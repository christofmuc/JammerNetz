#include "JammerNetzClientInfoMessage.h"

#include "flatbuffers/flatbuffers.h"
#include "JammerNetzPackages_generated.h"

// Deserializing constructor
JammerNetzClientInfoMessage::JammerNetzClientInfoMessage(uint8 *data, size_t bytes)
{
	flatbuffers::Verifier verifier(data + sizeof(JammerNetzHeader), bytes);
	if (VerifyJammerNetzPNPClientInfoPackageBuffer(verifier)) {
		auto root = GetJammerNetzPNPClientInfoPackage(data + sizeof(JammerNetzHeader));
		auto infos = root->clientInfos();
		for (auto info = infos->cbegin(); info != infos->cend(); info++) {
			auto ipData = info->ipAddress();
			jassert(ipData->size() == 16);
			if (ipData->size() == 16) {
				IPAddress ipAddress(ipData->data(), info->isIPV6());
				JammerNetzStreamQualityInfo qualityInfo;
				auto qi = info->qualityInfo();

				qualityInfo.tooLateOrDuplicate = qi->tooLateOrDuplicate();
				qualityInfo.droppedPacketCounter = qi->droppedPacketCounter();
				qualityInfo.outOfOrderPacketCounter = qi->outOfOrderPacketCounter();
				qualityInfo.duplicatePacketCounter = qi->duplicatePacketCounter();
				qualityInfo.dropsHealed = qi->dropsHealed();
				qualityInfo.packagesPushed = qi->packagesPushed();
				qualityInfo.packagesPopped = qi->packagesPopped();
				qualityInfo.maxLengthOfGap = qi->maxLengthOfGap();
				qualityInfo.maxWrongOrderSpan = qi->maxWrongOrderSpan();

				clientInfos_.emplace_back(ipAddress, info->portNumber(), qualityInfo);
			}
		}
	}
	else {
		jassert(false);
	}
}

JammerNetzClientInfoMessage::JammerNetzClientInfoMessage()
{
}

void JammerNetzClientInfoMessage::addClientInfo(IPAddress ipAddress, int port, JammerNetzStreamQualityInfo infoData)
{
	clientInfos_.emplace_back(ipAddress, port, infoData);
}

JammerNetzMessage::MessageType JammerNetzClientInfoMessage::getType() const
{
	return CLIENTINFO;
}

void JammerNetzClientInfoMessage::serialize(uint8 *output, size_t &byteswritten) const
{
	byteswritten += writeHeader(output, CLIENTINFO);

	flatbuffers::FlatBufferBuilder fbb;
	std::vector<flatbuffers::Offset<JammerNetzPNPClientInfo>> infos;
	for (auto clientInfo : clientInfos_) {
		// Setting the various fields of the quality info, separately
		JammerNetzPNPStreamQualityInfoBuilder quality(fbb);
		quality.add_tooLateOrDuplicate(clientInfo.qualityInfo.tooLateOrDuplicate);
		quality.add_droppedPacketCounter(clientInfo.qualityInfo.droppedPacketCounter);

		// Healed problems
		quality.add_outOfOrderPacketCounter(clientInfo.qualityInfo.outOfOrderPacketCounter);
		quality.add_duplicatePacketCounter(clientInfo.qualityInfo.duplicatePacketCounter);
		quality.add_dropsHealed(clientInfo.qualityInfo.dropsHealed);

		// Pure statistics
		quality.add_packagesPushed(clientInfo.qualityInfo.packagesPushed);
		quality.add_packagesPopped(clientInfo.qualityInfo.packagesPopped);
		quality.add_maxLengthOfGap(clientInfo.qualityInfo.maxLengthOfGap);
		quality.add_maxWrongOrderSpan(clientInfo.qualityInfo.maxWrongOrderSpan);
		auto qualityEnd = quality.Finish();

		auto ipVector = fbb.CreateVector(clientInfo.ipAddress, 16);
		JammerNetzPNPClientInfoBuilder info(fbb);
		info.add_ipAddress(ipVector);
		info.add_isIPV6(clientInfo.isIPV6);
		info.add_portNumber(clientInfo.portNumber);
		info.add_qualityInfo(qualityEnd);

		// Put into vector
		infos.push_back(info.Finish());
	}
	auto infoVec = fbb.CreateVector(infos);
	JammerNetzPNPClientInfoPackageBuilder infoPackage(fbb);
	infoPackage.add_clientInfos(infoVec);
	fbb.Finish(infoPackage.Finish());
	memcpy(output + byteswritten, fbb.GetBufferPointer(), fbb.GetSize());
	byteswritten += fbb.GetSize();
}

uint8 JammerNetzClientInfoMessage::getNumClients() const
{
	return (uint8)clientInfos_.size();
}

String JammerNetzClientInfoMessage::getIPAddress(uint8 clientNo) const
{
	if (clientNo < getNumClients()) {
		IPAddress address(clientInfos_[clientNo].ipAddress, clientInfos_[clientNo].isIPV6);
		return address.toString() + ":" + String(clientInfos_[clientNo].portNumber);
	}
	return "0.0.0.0";
}

JammerNetzStreamQualityInfo JammerNetzClientInfoMessage::getStreamQuality(uint8 clientNo) const
{
	JammerNetzStreamQualityInfo result{ 0 };
	if (clientNo < getNumClients()) {
		return clientInfos_[clientNo].qualityInfo;
	}
	return result;
}

JammerNetzClientInfo::JammerNetzClientInfo(IPAddress ip, int port, JammerNetzStreamQualityInfo qual) :
	portNumber(port), qualityInfo(qual)
{
	std::copy(ip.address, ip.address + 16, ipAddress);
	isIPV6 = ip.isIPv6;
}


