#include "Encryption.h"

#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>

namespace JammerNetzSecure {
namespace {

constexpr std::array<std::uint8_t, 8> KeyFileMagic{{'J', 'N', 'Z', 'K', 'E', 'Y', 0, 1}};
constexpr std::array<char, crypto_kdf_CONTEXTBYTES> KdfContext{{'J', 'N', 'Z', 'U', 'D', 'P', '0', '1'}};
constexpr std::array<std::uint8_t, 10> ClientToServerAad{{'J', 'N', 'Z', '-', 'C', '2', 'S', '-', 'v', '1'}};
constexpr std::array<std::uint8_t, 10> ServerToClientAad{{'J', 'N', 'Z', '-', 'S', '2', 'C', '-', 'v', '1'}};
constexpr std::uint8_t FormatVersion = 1;

std::span<const std::uint8_t> aadFor(const Direction direction)
{
	return direction == Direction::ClientToServer
		? std::span<const std::uint8_t>(ClientToServerAad)
		: std::span<const std::uint8_t>(ServerToClientAad);
}

bool isAllZero(const std::span<const std::uint8_t> bytes)
{
	std::uint8_t combined = 0;
	for (const auto byte : bytes) {
		combined = static_cast<std::uint8_t>(combined | byte);
	}
	return combined == 0;
}

bool deriveTrafficKey(const SessionKey& sessionKey, const Direction direction, TrafficKey& output)
{
	const auto subkeyId = direction == Direction::ClientToServer ? 1ULL : 2ULL;
	return crypto_kdf_derive_from_key(output.data(), output.size(), subkeyId,
		KdfContext.data(), sessionKey.masterKey().data()) == 0;
}

void randomNonce(std::span<std::uint8_t, SecureDatagramSealer::NonceBytes> nonce)
{
	randombytes_buf(nonce.data(), nonce.size());
}

void writeBigEndian64(std::uint8_t* output, const std::uint64_t value)
{
	for (std::size_t index = 0; index < 8; ++index) {
		output[index] = static_cast<std::uint8_t>(value >> (56U - static_cast<unsigned int>(index * 8U)));
	}
}

void writeBigEndian32(std::uint8_t* output, const std::uint32_t value)
{
	for (std::size_t index = 0; index < 4; ++index) {
		output[index] = static_cast<std::uint8_t>(value >> (24U - static_cast<unsigned int>(index * 8U)));
	}
}

std::uint64_t readBigEndian64(const std::uint8_t* input)
{
	std::uint64_t value = 0;
	for (std::size_t index = 0; index < 8; ++index) {
		value = (value << 8U) | input[index];
	}
	return value;
}

std::uint32_t readBigEndian32(const std::uint8_t* input)
{
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < 4; ++index) {
		value = (value << 8U) | input[index];
	}
	return value;
}

} // namespace

bool initializeCrypto() noexcept
{
	static const bool initialized = sodium_init() >= 0;
	return initialized;
}

SessionKey::SessionKey(const SessionId& sessionId, const MasterKey& masterKey)
	: sessionId_(sessionId), masterKey_(masterKey) {}

SessionKey::SessionKey(const SessionKey& other)
	: sessionId_(other.sessionId_), masterKey_(other.masterKey_) {}

SessionKey& SessionKey::operator=(const SessionKey& other)
{
	if (this != &other) {
		sodium_memzero(masterKey_.data(), masterKey_.size());
		sessionId_ = other.sessionId_;
		masterKey_ = other.masterKey_;
	}
	return *this;
}

SessionKey::~SessionKey() { sodium_memzero(masterKey_.data(), masterKey_.size()); }

std::shared_ptr<SessionKey> SessionKey::load(const std::filesystem::path& path, std::string& error)
{
	error.clear();
	if (!initializeCrypto()) {
		error = "could not initialize libsodium";
		return nullptr;
	}
#if !defined(_WIN32)
	std::error_code permissionError;
	const auto permissions = std::filesystem::status(path, permissionError).permissions();
	constexpr auto unsafePermissions = std::filesystem::perms::group_read
		| std::filesystem::perms::group_write | std::filesystem::perms::group_exec
		| std::filesystem::perms::others_read | std::filesystem::perms::others_write
		| std::filesystem::perms::others_exec;
	if (!permissionError && (permissions & unsafePermissions) != std::filesystem::perms::none) {
		std::cerr << "Warning: session key file is accessible by users other than its owner: "
			<< path.string() << std::endl;
	}
#endif
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) { error = "could not open session key file"; return nullptr; }
	if (input.tellg() != static_cast<std::streamoff>(SerializedSize)) {
		error = "session key file has the wrong length";
		return nullptr;
	}
	input.seekg(0);
	std::array<std::uint8_t, SerializedSize> serialized{};
	input.read(reinterpret_cast<char*>(serialized.data()), static_cast<std::streamsize>(serialized.size()));
	if (!input || !std::equal(KeyFileMagic.begin(), KeyFileMagic.end(), serialized.begin())) {
		error = "session key file has an unknown format or version";
		sodium_memzero(serialized.data(), serialized.size());
		return nullptr;
	}
	SessionId sessionId{};
	MasterKey masterKey{};
	std::copy_n(serialized.begin() + 8, sessionId.size(), sessionId.begin());
	std::copy_n(serialized.begin() + 24, masterKey.size(), masterKey.begin());
	const bool invalid = isAllZero(sessionId) || isAllZero(masterKey);
	sodium_memzero(serialized.data(), serialized.size());
	if (invalid) {
		error = "session key file contains an all-zero identifier or secret";
		sodium_memzero(masterKey.data(), masterKey.size());
		return nullptr;
	}
	auto result = std::make_shared<SessionKey>(sessionId, masterKey);
	sodium_memzero(masterKey.data(), masterKey.size());
	return result;
}

bool SessionKey::generate(const std::filesystem::path& path, const bool overwrite, std::string& error)
{
	error.clear();
	if (!initializeCrypto()) { error = "libsodium initialization failed"; return false; }
	std::error_code filesystemError;
	if (!overwrite && std::filesystem::exists(path, filesystemError)) {
		error = "refusing to overwrite an existing session key file";
		return false;
	}
	std::array<std::uint8_t, SerializedSize> serialized{};
	std::copy(KeyFileMagic.begin(), KeyFileMagic.end(), serialized.begin());
	randombytes_buf(serialized.data() + 8, 16);
	randombytes_buf(serialized.data() + 24, 32);
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		error = "could not create session key file";
		sodium_memzero(serialized.data(), serialized.size());
		return false;
	}
	output.write(reinterpret_cast<const char*>(serialized.data()), static_cast<std::streamsize>(serialized.size()));
	output.close();
	const bool success = static_cast<bool>(output);
	sodium_memzero(serialized.data(), serialized.size());
	if (!success) { error = "could not finish writing session key file"; return false; }
#if !defined(_WIN32)
	std::filesystem::permissions(path,
		std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
		std::filesystem::perm_options::replace, filesystemError);
#endif
	return true;
}

std::string SessionKey::fingerprint() const
{
	std::array<std::uint8_t, 8> digest{};
	crypto_generichash(digest.data(), digest.size(), masterKey_.data(), masterKey_.size(),
		sessionId_.data(), sessionId_.size());
	std::ostringstream output;
	output << std::hex << std::setfill('0');
	for (const auto byte : digest) output << std::setw(2) << static_cast<unsigned int>(byte);
	return output.str();
}

ReplayWindow::Result ReplayWindow::accept(const std::uint64_t counter) noexcept
{
	if (counter == 0) return {};
	if (!initialized_) {
		initialized_ = true;
		highestAccepted_ = counter;
		bitmap_[0] = 1;
		return {true, true};
	}
	if (counter > highestAccepted_) {
		const auto shift = counter - highestAccepted_;
		if (shift >= Width) bitmap_ = {};
		else if (shift >= 64) {
			bitmap_[1] = bitmap_[0] << (shift - 64);
			bitmap_[0] = 0;
		}
		else if (shift > 0) {
			bitmap_[1] = (bitmap_[1] << shift) | (bitmap_[0] >> (64 - shift));
			bitmap_[0] <<= shift;
		}
		bitmap_[0] |= 1;
		highestAccepted_ = counter;
		return {true, true};
	}
	const auto age = highestAccepted_ - counter;
	if (age >= Width) return {};
	const auto word = static_cast<std::size_t>(age / 64);
	const auto bit = static_cast<unsigned int>(age % 64);
	const std::uint64_t mask = 1ULL << bit;
	if ((bitmap_[word] & mask) != 0) return {};
	bitmap_[word] |= mask;
	return {true, false};
}

SenderInstanceId randomSenderInstanceId()
{
	SenderInstanceId id{};
	if (initializeCrypto()) randombytes_buf(id.data(), id.size());
	return id;
}

std::string senderInstanceIdString(const SenderInstanceId& id)
{
	std::array<char, 33> encoded{};
	sodium_bin2hex(encoded.data(), encoded.size(), id.data(), id.size());
	return encoded.data();
}

SecureDatagramSealer::SecureDatagramSealer(std::shared_ptr<const SessionKey> sessionKey,
	const Direction direction)
	: SecureDatagramSealer(std::move(sessionKey), direction, randomSenderInstanceId(), randomNonce) {}

SecureDatagramSealer::SecureDatagramSealer(std::shared_ptr<const SessionKey> sessionKey,
	const Direction direction, const SenderInstanceId& senderInstanceId, NonceSource nonceSource,
	const std::uint64_t initialCounter)
	: sessionKey_(std::move(sessionKey)), direction_(direction), senderInstanceId_(senderInstanceId),
	  nonceSource_(nonceSource == nullptr ? randomNonce : nonceSource), nextCounter_(initialCounter)
{
	initialized_ = sessionKey_ && initializeCrypto() && deriveTrafficKey(*sessionKey_, direction_, trafficKey_)
		&& !isAllZero(senderInstanceId_);
}

SecureDatagramSealer::~SecureDatagramSealer() { sodium_memzero(trafficKey_.data(), trafficKey_.size()); }

SecureDatagramResult SecureDatagramSealer::seal(const std::span<const std::uint8_t> payload,
	const std::span<std::uint8_t> wireOutput) noexcept
{
	if (!initialized_) return {sessionKey_ ? SecureDatagramError::CryptoUnavailable : SecureDatagramError::MissingKey};
	if (payload.size() > std::numeric_limits<std::uint32_t>::max()
		|| payload.size() + EnvelopeBytes > plaintext_.size()) return {SecureDatagramError::DatagramTooLarge};
	const auto required = payload.size() + WireOverhead;
	if (wireOutput.size() < required) return {SecureDatagramError::BufferTooSmall};
	std::lock_guard<std::mutex> lock(sealMutex_);
	if (exhausted_.load(std::memory_order_relaxed)) return {SecureDatagramError::CounterExhausted};
	const auto counter = nextCounter_.fetch_add(1, std::memory_order_relaxed);
	if (counter == 0 || counter == std::numeric_limits<std::uint64_t>::max()) {
		exhausted_.store(true, std::memory_order_relaxed);
		return {SecureDatagramError::CounterExhausted};
	}
	Nonce nonce{};
	nonceSource_(nonce);
	std::copy(nonce.begin(), nonce.end(), wireOutput.begin());
	std::fill_n(plaintext_.begin(), EnvelopeBytes, 0);
	plaintext_[0] = FormatVersion;
	std::copy(sessionKey_->sessionId().begin(), sessionKey_->sessionId().end(), plaintext_.begin() + 4);
	std::copy(senderInstanceId_.begin(), senderInstanceId_.end(), plaintext_.begin() + 20);
	writeBigEndian64(plaintext_.data() + 36, counter);
	writeBigEndian32(plaintext_.data() + 40, static_cast<std::uint32_t>(payload.size()));
	std::copy(payload.begin(), payload.end(), plaintext_.begin() + static_cast<std::ptrdiff_t>(EnvelopeBytes));
	unsigned long long ciphertextBytes = 0;
	const auto aad = aadFor(direction_);
	const int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
		wireOutput.data() + NonceBytes, &ciphertextBytes,
		plaintext_.data(), static_cast<unsigned long long>(EnvelopeBytes + payload.size()),
		aad.data(), static_cast<unsigned long long>(aad.size()), nullptr, nonce.data(), trafficKey_.data());
	sodium_memzero(plaintext_.data(), EnvelopeBytes + payload.size());
	if (result != 0 || ciphertextBytes != EnvelopeBytes + payload.size() + TagBytes)
		return {SecureDatagramError::CryptoUnavailable};
	return {SecureDatagramError::None, required, {senderInstanceId_, counter, true}};
}

SecureDatagramOpener::SecureDatagramOpener(std::shared_ptr<const SessionKey> sessionKey,
	const Direction direction) : sessionKey_(std::move(sessionKey)), direction_(direction)
{
	initialized_ = sessionKey_ && initializeCrypto() && deriveTrafficKey(*sessionKey_, direction_, trafficKey_);
}

SecureDatagramOpener::~SecureDatagramOpener() { sodium_memzero(trafficKey_.data(), trafficKey_.size()); }

SecureDatagramResult SecureDatagramOpener::open(const std::span<const std::uint8_t> wire,
	const std::span<std::uint8_t> payloadOutput) noexcept
{
	if (!initialized_) return {sessionKey_ ? SecureDatagramError::CryptoUnavailable : SecureDatagramError::MissingKey};
	if (wire.size() < WireOverhead) return {SecureDatagramError::DatagramTooShort};
	const auto plaintextCapacity = wire.size() - SecureDatagramSealer::NonceBytes - SecureDatagramSealer::TagBytes;
	if (plaintextCapacity > plaintext_.size()) return {SecureDatagramError::DatagramTooLarge};
	unsigned long long plaintextBytes = 0;
	const auto aad = aadFor(direction_);
	const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
		plaintext_.data(), &plaintextBytes, nullptr,
		wire.data() + SecureDatagramSealer::NonceBytes,
		static_cast<unsigned long long>(wire.size() - SecureDatagramSealer::NonceBytes),
		aad.data(), static_cast<unsigned long long>(aad.size()), wire.data(), trafficKey_.data());
	if (result != 0) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {SecureDatagramError::AuthenticationFailed};
	}
	if (plaintextBytes < SecureDatagramSealer::EnvelopeBytes) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {SecureDatagramError::InvalidEnvelope};
	}
	const auto payloadBytes = static_cast<std::size_t>(plaintextBytes) - SecureDatagramSealer::EnvelopeBytes;
	const bool invalidHeader = plaintext_[0] != FormatVersion || plaintext_[1] != 0
		|| plaintext_[2] != 0 || plaintext_[3] != 0 || readBigEndian32(plaintext_.data() + 40) != payloadBytes;
	if (invalidHeader || payloadOutput.size() < payloadBytes) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {invalidHeader ? SecureDatagramError::InvalidEnvelope : SecureDatagramError::BufferTooSmall};
	}
	if (!std::equal(sessionKey_->sessionId().begin(), sessionKey_->sessionId().end(), plaintext_.begin() + 4)) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {SecureDatagramError::WrongSession};
	}
	SecureDatagramMetadata metadata;
	std::copy_n(plaintext_.begin() + 20, metadata.senderInstanceId.size(), metadata.senderInstanceId.begin());
	metadata.securityCounter = readBigEndian64(plaintext_.data() + 36);
	if (isAllZero(metadata.senderInstanceId)) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {SecureDatagramError::InvalidEnvelope};
	}
	const auto replayResult = replayWindows_[metadata.senderInstanceId].accept(metadata.securityCounter);
	if (!replayResult.accepted) {
		sodium_memzero(plaintext_.data(), plaintextCapacity);
		return {SecureDatagramError::Replay, 0, metadata};
	}
	metadata.advancedHighWatermark = replayResult.advancedHighWatermark;
	std::copy_n(plaintext_.begin() + static_cast<std::ptrdiff_t>(SecureDatagramSealer::EnvelopeBytes),
		payloadBytes, payloadOutput.begin());
	sodium_memzero(plaintext_.data(), plaintextCapacity);
	return {SecureDatagramError::None, payloadBytes, metadata};
}

} // namespace JammerNetzSecure
