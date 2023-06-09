﻿/*
	Reliability and Flow Control Example
	From "Networking for Game Programmers" - http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <vector>

#include "Net.h"
#include "crc.h"
#pragma warning(disable : 4996)

//#define SHOW_ACKS

using namespace std;
using namespace net;

const int ServerPort = 30000;
const int ClientPort = 30001;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
const int PacketSize = 256;

const int maxFileName = 16;
const int maxFileSize = 16;
const int maxLine = PacketSize - (maxFileName + maxFileSize);

class FlowControl
{
public:

	FlowControl()
	{
		printf("flow control initialized\n");
		Reset();
	}

	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}

	void Update(float deltaTime, float rtt)
	{
		const float RTT_Threshold = 250.0f;

		if (mode == Good)
		{
			if (rtt > RTT_Threshold)
			{
				printf("*** dropping to bad mode ***\n");
				mode = Bad;
				if (good_conditions_time < 10.0f && penalty_time < 60.0f)
				{
					penalty_time *= 2.0f;
					if (penalty_time > 60.0f)
						penalty_time = 60.0f;
					printf("penalty time increased to %.1f\n", penalty_time);
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}

			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;

			if (penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f)
			{
				penalty_time /= 2.0f;
				if (penalty_time < 1.0f)
					penalty_time = 1.0f;
				printf("penalty time reduced to %.1f\n", penalty_time);
				penalty_reduction_accumulator = 0.0f;
			}
		}

		if (mode == Bad)
		{
			if (rtt <= RTT_Threshold)
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;

			if (good_conditions_time > penalty_time)
			{
				printf("*** upgrading to good mode ***\n");
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				mode = Good;
				return;
			}
		}
	}

	float GetSendRate()
	{
		return mode == Good ? 30.0f : 10.0f;
	}

private:

	enum Mode
	{
		Good,
		Bad
	};

	Mode mode;
	float penalty_time;
	float good_conditions_time;
	float penalty_reduction_accumulator;
};

// ----------------------------------------------

int main(int argc, char* argv[])
{
	// parse command line

	enum Mode
	{
		Client,
		Server
	};

	Mode mode = Server;
	Address address;
	char fileName[maxFileName];

	// add one more argument for the file name
	if (argc >= 3)
	{
		int a, b, c, d;
#pragma warning(suppress : 4996)
		if (sscanf(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d))
		{
			mode = Client;
			address = Address(a, b, c, d, ServerPort);
		}
		else
		{
			printf("failed to read IP address\n");
			return 1;
		}

		strcpy(fileName, argv[2]);

	}

	// initialize

	if (!InitializeSockets())
	{
		printf("failed to initialize sockets\n");
		return 1;
	}

	ReliableConnection connection(ProtocolId, TimeOut);

	const int port = mode == Server ? ServerPort : ClientPort;

	if (!connection.Start(port))
	{
		printf("could not start connection on port %d\n", port);
		return 1;
	}

	unsigned char packet[PacketSize];

	if (mode == Client)
	{
		connection.Connect(address);

	}
	else
		connection.Listen();



	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;

	FlowControl flowControl;

	while (true)
	{
		// update flow control

		if (connection.IsConnected())
			flowControl.Update(DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f);

		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if (mode == Server && connected && !connection.IsConnected())
		{
			flowControl.Reset();
			printf("reset flow control\n");
			connected = false;
		}

		if (!connected && connection.IsConnected())
		{
			printf("client connected to server\n");
			connected = true;
		}

		if (!connected && connection.ConnectFailed())
		{
			printf("connection failed\n");
			break;
		}

		// send and receive packets
		sendAccumulator += DeltaTime;

		char fileSize[maxFileSize];
		char contents[maxLine] = "";

		while (sendAccumulator > 1.0f / sendRate)
		{
			if (mode == Server)
				break;

			// open the file
			FILE* pFile = NULL;
	
			pFile = fopen(fileName, "r");
			if (pFile == NULL)
			{
				printf("ERROR: file does not exist");
				break;
			}
				
			// check the size of the file
			int intfileSize = 0;

			fseek(pFile, 0, SEEK_END);
			intfileSize = ftell(pFile);
			fseek(pFile, 0, SEEK_SET);

			itoa(intfileSize, fileSize, 10);

			// declare packet
			unsigned char packet[PacketSize];
			memset(packet, 0, PacketSize);

			// read the file contents
			char ch;
			while (!feof(pFile))
			{
				memset(contents, 0, maxLine);
				fread(contents, sizeof(char), maxLine, pFile);

				// copy the file metadata
				memcpy(packet, fileName, maxFileName);
				memcpy(packet + maxFileName, fileSize, maxFileSize);

				// break the file in pieces
				memcpy(packet + maxFileName + maxFileSize, contents, maxLine);

				// send the pieces until the file end
				crcSlow(packet, sizeof(packet));
				connection.SendPacket(packet, sizeof(packet));

			}

			sendAccumulator -= 1.0f / sendRate;
		}
		
		FILE* outFile = NULL;
		int currentFileSize = 0;

		while (true)
		{
			unsigned char packet[PacketSize];
			memset(packet, 0, PacketSize);

			// receive the packet data
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0)
				break;

			crcSlow(packet, sizeof(packet));

			// get the file metadata
			char fileName[maxFileName];
			char fileSize[maxFileSize];

			// \0 
			for (int i = 0; i < maxFileName; i++)
			{
				fileName[i] = packet[i];
			}
			for (int i = 0; i < maxFileSize; i++)
			{
				fileSize[i] = packet[i+maxFileName];
			}

			int intFileSize = atoi(fileSize);

			// get the file contents
			char fileContents[maxLine];
			memset(fileContents, 0, maxLine);

			for (int i = 0; i < maxLine; i++)
			{
				fileContents[i] = packet[i+maxFileName+maxFileSize];
			}

			// create a file
			if (outFile == NULL)
			{
				outFile = fopen(fileName, "w");
			}
			
			if (currentFileSize < intFileSize)
			{
				fwrite(fileContents, sizeof(char), sizeof(fileContents), outFile);
				currentFileSize += sizeof(fileContents);
			}

			printf("%s %s %s\n", fileName, fileSize, fileContents);

			// TODO: verify the file integrity
		}

		if (outFile != NULL)
		{
			fclose(outFile);
		}

		// show packets that were acked this frame

#ifdef SHOW_ACKS
		unsigned int* acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks(&acks, ack_count);
		if (ack_count > 0)
		{
			printf("acks: %d", acks[0]);
			for (int i = 1; i < ack_count; ++i)
				printf(",%d", acks[i]);
			printf("\n");
		}
#endif

		// update connection

		connection.Update(DeltaTime);

		// show connection stats

		statsAccumulator += DeltaTime;

		while (statsAccumulator >= 0.25f && connection.IsConnected())
		{
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

			printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
				sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
				sent_bandwidth, acked_bandwidth);

			statsAccumulator -= 0.25f;
		}

		net::wait(DeltaTime);
	}

	ShutdownSockets();

	return 0;
}
