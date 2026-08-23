/*
   Copyright (c) 2026 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace JammerNetzSecure {

using SessionId = std::array<std::uint8_t, 16>;
using SenderInstanceId = std::array<std::uint8_t, 16>;
using MasterKey = std::array<std::uint8_t, 32>;
using TrafficKey = std::array<std::uint8_t, 32>;
using Nonce = std::array<std::uint8_t, 24>;

enum class Direction { ClientToServer, ServerToClient };

enum class SecureDatagramError {
	None,
	CryptoUnavailable,
	MissingKey,
	BufferTooSmall,
	DatagramTooShort,
	DatagramTooLarge,
	AuthenticationFailed,
	InvalidEnvelope,
	WrongSession,
	Replay,
	CounterExhausted
};

struct SecureDatagramMetadata {
	SenderInstanceId senderInstanceId{};
	std::uint64_t securityCounter{0};
	bool advancedHighWatermark{false};
};

struct SecureDatagramResult {
	SecureDatagramError error{SecureDatagramError::None};
	std::size_t bytesWritten{0};
	SecureDatagramMetadata metadata{};
	explicit operator bool() const noexcept { return error == SecureDatagramError::None; }
};

class SessionKey {
public:
	static constexpr std::size_t SerializedSize = 56;

	SessionKey(const SessionId& sessionId, const MasterKey& masterKey);
	SessionKey(const SessionKey& other);
	SessionKey& operator=(const SessionKey& other);
	~SessionKey();

	static std::shared_ptr<SessionKey> load(const std::filesystem::path& path, std::string& error);
	static bool generate(const std::filesystem::path& path, bool overwrite, std::string& error);

	const SessionId& sessionId() const noexcept { return sessionId_; }
	const MasterKey& masterKey() const noexcept { return masterKey_; }
	std::string fingerprint() const;

private:
	SessionId sessionId_{};
	MasterKey masterKey_{};
};

class ReplayWindow {
public:
	static constexpr std::size_t Width = 128;
	struct Result { bool accepted{false}; bool advancedHighWatermark{false}; };
	Result accept(std::uint64_t counter) noexcept;
	std::uint64_t highestAccepted() const noexcept { return highestAccepted_; }

private:
	std::uint64_t highestAccepted_{0};
	std::array<std::uint64_t, 2> bitmap_{};
	bool initialized_{false};
};

class SecureDatagramSealer {
public:
	static constexpr std::size_t NonceBytes = 24;
	static constexpr std::size_t TagBytes = 16;
	static constexpr std::size_t EnvelopeBytes = 44;
	static constexpr std::size_t WireOverhead = NonceBytes + TagBytes + EnvelopeBytes;
	static constexpr std::size_t MaximumPlaintextBytes = 65536;
	using NonceSource = void (*)(std::span<std::uint8_t, NonceBytes>);

	SecureDatagramSealer(std::shared_ptr<const SessionKey> sessionKey, Direction direction);
	SecureDatagramSealer(std::shared_ptr<const SessionKey> sessionKey, Direction direction,
		const SenderInstanceId& senderInstanceId, NonceSource nonceSource,
		std::uint64_t initialCounter = 1);
	~SecureDatagramSealer();
	SecureDatagramResult seal(std::span<const std::uint8_t> payload,
		std::span<std::uint8_t> wireOutput) noexcept;
	const SenderInstanceId& senderInstanceId() const noexcept { return senderInstanceId_; }

private:
	std::shared_ptr<const SessionKey> sessionKey_;
	TrafficKey trafficKey_{};
	Direction direction_;
	SenderInstanceId senderInstanceId_{};
	NonceSource nonceSource_{nullptr};
	std::atomic<std::uint64_t> nextCounter_{1};
	std::atomic<bool> exhausted_{false};
	std::array<std::uint8_t, MaximumPlaintextBytes> plaintext_{};
	std::mutex sealMutex_;
	bool initialized_{false};
};

class SecureDatagramOpener {
public:
	static constexpr std::size_t WireOverhead = SecureDatagramSealer::WireOverhead;
	SecureDatagramOpener(std::shared_ptr<const SessionKey> sessionKey, Direction direction);
	~SecureDatagramOpener();
	SecureDatagramResult open(std::span<const std::uint8_t> wire,
		std::span<std::uint8_t> payloadOutput) noexcept;

private:
	std::shared_ptr<const SessionKey> sessionKey_;
	TrafficKey trafficKey_{};
	Direction direction_;
	std::map<SenderInstanceId, ReplayWindow> replayWindows_;
	std::array<std::uint8_t, SecureDatagramSealer::MaximumPlaintextBytes> plaintext_{};
	bool initialized_{false};
};

bool initializeCrypto() noexcept;
SenderInstanceId randomSenderInstanceId();
std::string senderInstanceIdString(const SenderInstanceId& id);

} // namespace JammerNetzSecure
