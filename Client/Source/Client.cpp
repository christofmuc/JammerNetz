/*
   Copyright (c) 2019 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "Client.h"

#include "JammerNetzPackage.h"
#include "ServerInfo.h"
#include "StreamLogger.h"

#include "BuffersConfig.h"
#include "XPlatformUtils.h"

#include <algorithm>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#elif JUCE_LINUX || JUCE_MAC
#include <netinet/in.h>
#include <sys/socket.h>
#endif

Client::Client(DatagramSocket& socket) : socket_(socket), messageCounter_(10) /* TODO - because of the pre-fill on server side, can't be 0 */
	, currentBlockSize_(0), useFEC_(false), serverPort_(7777), useLocalhost_(false), fecBuffer_(16)
{
}

Client::~Client()
{
}

void Client::setServer(const juce::String& serverName, int serverPort, bool useLocalhost)
{
	const juce::ScopedLock lock(serverLock_);
	serverName_ = serverName;
	serverPort_.store(serverPort > 0 ? serverPort : 7777, std::memory_order_relaxed);
	useLocalhost_.store(useLocalhost, std::memory_order_relaxed);
}

void Client::setUseFEC(bool enabled)
{
	useFEC_.store(enabled, std::memory_order_relaxed);
	nlohmann::json fecControl;
	fecControl["FEC"] = enabled;
	sendControl(fecControl);
}

void Client::setSessionKey(std::shared_ptr<const JammerNetzSecure::SessionKey> sessionKey)
{
	ScopedLock cryptoLock(cryptoLock_);
	sealer_ = sessionKey
		? std::make_unique<JammerNetzSecure::SecureDatagramSealer>(
			std::move(sessionKey), JammerNetzSecure::Direction::ClientToServer)
		: nullptr;
}

bool Client::sendData(String const &remoteHostname, int remotePort, void *data, int numbytes) {
	// Writing will block until the socket is ready to write
	auto bytesWritten = socket_.write(remoteHostname, remotePort, data, numbytes);
	if (bytesWritten == -1 || bytesWritten != numbytes) {
		// This is bad - when could this happen?
		// Well, for once, if the remoteHostname is empty or incorrect
		return false;
	}
	return true;
}

bool Client::sendData(JammerNetzChannelSetup const& channelSetup, std::shared_ptr<AudioBuffer<float>> audioBuffer, ControlData controllers) {
    ScopedLock lockSocket(socketLock_);

    // If we have FEC data, and the user enabled it, append the last block sent
    std::shared_ptr<AudioBlock> fecBlock;
    //if (useFEC_ && !fecBuffer_.isEmpty()) {
    //    fecBlock = fecBuffer_.getLast();
    //}
    MidiSignal toSend = MidiSignal_None;
    if (controllers.midiSignal.has_value()) {
        toSend = *controllers.midiSignal;
    }

    // Create a message
    JammerNetzAudioData audioMessage(messageCounter_, Time::getMillisecondCounterHiRes(), channelSetup, SAMPLE_RATE,
                                     controllers.bpm, toSend, audioBuffer, fecBlock);

    messageCounter_++;
    size_t totalBytes;
    audioMessage.serialize(plaintextBuffer_, totalBytes);

    // Store the audio data somewhere else because we need it for forward error correction
    std::shared_ptr<AudioBlock> redundencyData = std::make_shared<AudioBlock>();
    redundencyData->messageCounter = audioMessage.messageCounter();
    redundencyData->timestamp = audioMessage.timestamp();
    redundencyData->channelSetup = channelSetup;
    redundencyData->audioBuffer = std::make_shared<AudioBuffer<float>>();
    *redundencyData->audioBuffer = *audioBuffer; // Deep copy
    fecBuffer_.push(redundencyData);
	const bool sent = sendBufferToServer(totalBytes);
	maybeSendMtuProbe();
	return sent;
}

bool Client::sendBufferToServer(size_t totalBytes)
{
	// Send off to server
	String servername;
	int serverPort;
	bool useLocalhost;
	{
		ScopedLock lock(serverLock_);
		servername = serverName_;
		serverPort = serverPort_.load(std::memory_order_relaxed);
		useLocalhost = useLocalhost_.load(std::memory_order_relaxed);
	}

	if (useLocalhost) {
		servername = "127.0.0.1";
	}

	ScopedLock cryptoLock(cryptoLock_);
	if (!sealer_) {
		return false;
	}
	const auto sealed = sealer_->seal(
		std::span<const uint8>(plaintextBuffer_, totalBytes), std::span<uint8>(wireBuffer_));
	if (!sealed || !sizet_is_safe_as_int(sealed.bytesWritten)) {
		return false;
	}
	const int bytesToSend = static_cast<int>(sealed.bytesWritten);
	currentBlockSize_ = bytesToSend;
	return sendData(servername, serverPort, wireBuffer_, bytesToSend);
}

bool Client::sendControl(nlohmann::json &json)
{
    ScopedLock lockSocket(socketLock_);

    JammerNetzControlMessage controlMessage(json);
    size_t totalBytes;
    controlMessage.serialize(plaintextBuffer_, totalBytes);
    if (totalBytes > 0) {
        return sendBufferToServer(totalBytes);
    }
    else
    {
        return false;
    }
}

int Client::getCurrentBlockSize() const
{
	return currentBlockSize_;
}

void Client::setMtuDiscoverySupported(bool supported)
{
	const ScopedLock lock(mtuDiscoveryLock_);
	if (supported && mtuDiscovery_.status() == PathMtuDiscoveryStatus::Unavailable
		&& !enableDoNotFragment()) {
		mtuDiscovery_.markFailed();
		return;
	}
	mtuDiscovery_.setSupported(supported);
}

void Client::acknowledgeMtuProbe(uint64 probeId, int payloadBytes)
{
	const ScopedLock lock(mtuDiscoveryLock_);
	mtuDiscovery_.acknowledge(probeId, payloadBytes);
}

int Client::getSafeUdpPayloadSize() const
{
	const ScopedLock lock(mtuDiscoveryLock_);
	return mtuDiscovery_.safePayloadBytes();
}

PathMtuDiscoveryStatus Client::getMtuDiscoveryStatus() const
{
	const ScopedLock lock(mtuDiscoveryLock_);
	return mtuDiscovery_.status();
}

void Client::maybeSendMtuProbe()
{
	std::optional<PathMtuProbe> probe;
	{
		const ScopedLock lock(mtuDiscoveryLock_);
		probe = mtuDiscovery_.poll();
	}
	if (probe.has_value()) {
		sendMtuProbe(*probe);
	}
}

bool Client::sendMtuProbe(const PathMtuProbe& probe)
{
	nlohmann::json json;
	json["mtu_probe_v1"]["id"] = probe.id;
	json["mtu_probe_v1"]["size"] = probe.payloadBytes;

	const ScopedLock cryptoLock(cryptoLock_);
	if (!sealer_) {
		return false;
	}
	auto serializeWithPadding = [&](int paddingBytes, size_t& plaintextBytes) {
		json["mtu_probe_v1"]["padding"] = std::string(static_cast<size_t>(paddingBytes), 'p');
		JammerNetzControlMessage message(json);
		message.serialize(plaintextBuffer_, plaintextBytes);
	};
	auto wireSizeFor = [&](size_t plaintextBytes) {
		return static_cast<int>(plaintextBytes + JammerNetzSecure::SecureDatagramSealer::WireOverhead);
	};

	size_t emptyPlaintextBytes = 0;
	serializeWithPadding(0, emptyPlaintextBytes);
	const int estimate = (std::max)(0, probe.payloadBytes - wireSizeFor(emptyPlaintextBytes));
	const int firstPadding = (std::max)(0, estimate - 64);
	const int lastPadding = (std::min)(probe.payloadBytes, estimate + 64);
	for (int padding = firstPadding; padding <= lastPadding; ++padding) {
		size_t plaintextBytes = 0;
		serializeWithPadding(padding, plaintextBytes);
		if (wireSizeFor(plaintextBytes) != probe.payloadBytes) {
			continue;
		}

		const auto sealed = sealer_->seal(
			std::span<const uint8>(plaintextBuffer_, plaintextBytes), std::span<uint8>(wireBuffer_));
		const int wireBytes = sealed && sizet_is_safe_as_int(sealed.bytesWritten)
			? static_cast<int>(sealed.bytesWritten) : -1;
		if (wireBytes != probe.payloadBytes) {
			return false;
		}

		String serverName;
		int serverPort;
		bool useLocalhost;
		{
			const ScopedLock serverLock(serverLock_);
			serverName = serverName_;
			serverPort = serverPort_.load(std::memory_order_relaxed);
			useLocalhost = useLocalhost_.load(std::memory_order_relaxed);
		}
		if (useLocalhost) {
			serverName = "127.0.0.1";
		}
		return sendData(serverName, serverPort, wireBuffer_, wireBytes);
	}
	return false;
}

bool Client::enableDoNotFragment()
{
#if JUCE_WINDOWS
	const DWORD mode = IP_PMTUDISC_PROBE;
	return setsockopt(static_cast<SOCKET>(socket_.getRawSocketHandle()), IPPROTO_IP,
		IP_MTU_DISCOVER, reinterpret_cast<const char*>(&mode), sizeof(mode)) == 0;
#elif JUCE_LINUX
	const int mode = IP_PMTUDISC_PROBE;
	return setsockopt(socket_.getRawSocketHandle(), IPPROTO_IP, IP_MTU_DISCOVER,
		&mode, sizeof(mode)) == 0;
#elif JUCE_MAC && defined(IP_DONTFRAG)
	const int enabled = 1;
	return setsockopt(socket_.getRawSocketHandle(), IPPROTO_IP, IP_DONTFRAG,
		&enabled, sizeof(enabled)) == 0;
#else
	return false;
#endif
}
