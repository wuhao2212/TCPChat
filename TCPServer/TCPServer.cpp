#include "TCPServer.h"
#include <thread>
#include <chrono>
#include <string>
#include <string.h>
#include <opencv2/opencv.hpp>
#include <QApplication>
#include <QFile>
#include <QXmlStreamReader>


namespace Server {
	TCPServer::TCPServer()
	{
		m_applicationPath = QApplication::applicationDirPath();

		if (!ReadSettings())
		{
			std::cerr << "Can't read settings" << std::endl;
			exit(1);
		}

		InitializeWSA();
		SetupSockaddr();
		CreateBindListenSocket();
	}
	TCPServer::TCPServer(int port, const char * ip) :
		m_ip(ip),
		m_port(port)
	{
		m_applicationPath = QApplication::applicationDirPath();

		InitializeWSA();
		SetupSockaddr();
		CreateBindListenSocket();
	}

	void TCPServer::SetupSockaddr()
	{
		m_addr.sin_addr.s_addr = ADDR_ANY;
		m_addr.sin_port = htons(m_port);
		m_addr.sin_family = AF_INET;
	}


	bool TCPServer::Run()
	{
		const std::string c_welcome = "WELCOME!";
		std::cout << "Server start!" << std::endl;
		ShowServerInformation();

		int sizeOfAddr = sizeof(m_addr);
		while (true)
		{
			SOCKET newClient = 0;
			if (newClient = accept(m_listenSocket, (SOCKADDR*)&m_addr, &sizeOfAddr))
			{

				//Receive packet of the client
				PacketType packet;
				RecievePacket(newClient, packet);

				if (packet == P_ChatMessage)
				{
					//Receive name of the client
					std::string name;
					bool result = RecieveMessage(newClient, name);

					if (result)
					{
						//Insert a new client
						int id = 0;
						bool insertStatus = DB::GetInstance().InserClientInfo(name, id);

						if (insertStatus)
						{
							//Send connected
							if (!this->SendInformationMessage(newClient, "Connected"))
							{
								continue;
							}

							std::cout << "Client connected!" << std::endl;

							//Receive setup message
							RecievePacket(newClient, packet);
							if (packet == P_InformationMessage)
							{
								if (!ProcessInformationMessage(newClient))
								{
									continue;
								}
							}

							if (m_videoCounter != 0)
							{
								if (!this->SendInformationMessage(newClient, "Start Video"))
								{
									continue;
								}
							}


							//Add new client to container
							{
								std::lock_guard<std::mutex> lock(m_mutex);
								m_clients.push_back(newClient);
								m_names.push_back(name);
							}

							SendClientsList();

							//Create thread for new client
							std::thread td(CreateHandler(), int(m_clients.size() - 1));

							//detach thread
							td.detach();
						}
						else
						{
							if (!this->SendInformationMessage(newClient, "Client with the same name exist"))
							{
								continue;
							}
						}
					}
				}

			}
			else
			{
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

	}

	void TCPServer::ShowServerInformation()
	{
		std::cout << "Information: " << "\n";
		std::cout << "Ip: " << m_ip << "\n";
		std::cout << "Port: " << m_port << "\n";

		return;
	}

	void TCPServer::RecieveFrame(SOCKET client, std::vector<uchar>& data)
	{
		std::vector<uchar> buffer;
		int frameSize;

		//recieve size
		int resultInt = recv(client, (char*)&frameSize, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Client disconnect " << GetLastError() << std::endl;

		}

		buffer.resize(frameSize);

		//recieve char
		int resultFrame = recv(client, (char*)buffer.data(), frameSize, MSG_WAITALL);

		if (resultFrame == SOCKET_ERROR)
		{
			std::cerr << "Error: recieve frame!" << std::endl;


		}

		data.resize(frameSize);
		data = buffer;
	}

	bool TCPServer::SendFrame(SOCKET client, std::vector<uchar> data)
	{
		PacketType packet = P_FrameMessage;

		int resultPacket = send(client, (char*)&packet, sizeof(packet), NULL);
		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Don't send a packet message" << GetLastError() << std::endl;
			return false;
		}

		int imgSize = static_cast<int>(data.size());

		int resultInt = send(client, (char*)&imgSize, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Don't send int message for client" << GetLastError() << std::endl;
			return false;
		}

		int result = send(client, (char*)data.data(), imgSize, NULL);

		if (result == SOCKET_ERROR)
		{
			std::cerr << "Don't send message for client" << GetLastError() << std::endl;
			return false;
		}

		return true;
	}

	bool TCPServer::ReceiveAudio(SOCKET client, char **buffer, int &length)
	{
		int bufferSize = 0;

		int resultInt = recv(client, (char*)&bufferSize, sizeof(int), NULL);
		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Can't receive size of buffer" << GetLastError() << std::endl;
			return false;
		}

		char *tempBuffer = new char[bufferSize + 1];
		tempBuffer[bufferSize] = '\0';

		int resultBuffer = recv(client, tempBuffer, bufferSize, NULL);
		if (resultBuffer == SOCKET_ERROR)
		{
			std::cerr << "Can't receive bytes of audio" << GetLastError() << std::endl;
			return false;
		}

		*buffer = tempBuffer;
		length = bufferSize;

		return true;
	}

	void TCPServer::SendAudio(SOCKET client, char * buffer, int length)
	{

		PacketType packet = P_AudioMessage;

		int resultPacket = send(client, (char*)&packet, sizeof(packet), NULL);
		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Audio: can't send a packet " << GetLastError() << std::endl;
			return;
		}

		int size = length;
		int resultInt = send(client, (char*)&size, sizeof(int), NULL);
		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Audio: can't send a buffer size " << GetLastError() << std::endl;
			return;
		}

		int resultBuffer = send(client, buffer, size, NULL);
		if (resultBuffer == SOCKET_ERROR)
		{
			std::cerr << "Audio: can't send a buffer" << GetLastError() << std::endl;
			return;
		}



		return;
	}

	bool TCPServer::ProcessInformationMessage(SOCKET client)
	{
		std::string message;
		bool resultMessage = this->RecieveMessage(client, message);

		if (!resultMessage)
		{
			return false;
		}

		if (message.compare("Stop Video") == 0)
		{
			--m_videoCounter;

			std::cout << m_videoCounter << std::endl;

			if (m_videoCounter < 2)
			{
				m_multipleMode = false;

				this->SendAllInformationMessage("Single Mode");

				if (m_videoCounter == 0)
				{
					this->SendAllWithoutClientInformationMessage(client, "Stop Video");
					this->SendAllInformationMessage("Hide");
				}
			}
			return true;
		}
		else if (message.compare("Start Video") == 0)
		{
			++m_videoCounter;

			if (m_clients.size() > 2)
			{
				if (m_videoCounter >= 2)
				{
					m_multipleMode = true;

					this->SendAllInformationMessage("Multiple mode");
					
					return true;
				}
			}

			m_multipleMode = false;

			this->SendAllWithoutClientInformationMessage(client, "Start Video");

			return true;
		}
		else if (message.compare("Setup") == 0)
		{
			return true;
		}
		else if (message.compare("Save History") == 0)
		{
			this->SaveMessageHistoryOfClient(client);
			return true;
		}
		else if (message.compare("History") == 0)
		{
			std::string clientName;
			PacketType packet;

			//if (ProcessPacket(client, packet))
			//{
			//	if (packet == P_ChatMessage)
			//	{
			//		RecieveMessage(client, clientName);
			//
			//		//Get History from database
			//
			//		//
			//	}
			//
			//}


		}

		return false;
	}

	bool TCPServer::SendInformationMessage(SOCKET client, std::string message)
	{
		PacketType packet = P_InformationMessage;

		int resultPacket = send(client, (char*)&packet, sizeof(packet), NULL);
		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Information: can't send a packet" << GetLastError() << std::endl;
			return false;
		}

		int messageSize = static_cast<int>(message.size());
		int resultInt = send(client, (char*)&messageSize, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Don't send int message for client" << GetLastError() << std::endl;
			return false;
		}

		int result = send(client, message.c_str(), messageSize, NULL);

		if (result == SOCKET_ERROR)
		{
			std::cerr << "Don't send message for client" << GetLastError() << std::endl;
			return false;
		}

		return true;
	}

	void TCPServer::SendAllWithoutClientInformationMessage(SOCKET client, std::string message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (int i = 0; i < m_clients.size(); i++)
		{
			if (m_clients[i] == client)
			{
				continue;
			}
			else if (!this->SendInformationMessage(m_clients[i], message))
			{
				break;
			}
		}
	}

	void TCPServer::SendAllInformationMessage(std::string message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (int i = 0; i < m_clients.size(); ++i)
		{
			if (!this->SendInformationMessage(m_clients[i], message))
			{
				break;
			}
		}
	}

	void TCPServer::RecievePacket(SOCKET client, PacketType & packet)
	{
		int resultPacket = recv(client, (char*)&packet, sizeof(packet), NULL);

		if (resultPacket == SOCKET_ERROR)
		{
			std::cout << "RecievePacket: error to receive packet" << GetLastError() << std::endl;
		}

		return;
	}

	void TCPServer::SendClientsList()
	{
		this->SendAllInformationMessage("List");

		std::string strList;
		auto listOfClients = DB::GetInstance().SelectNameOfAllClients();

		for (auto &clientName : listOfClients)
		{
			strList.append(clientName);
			strList.append(" ");
		}

		this->SendAllInformationMessage(strList);
	}

	bool TCPServer::ReadSettings()
	{
		QString settingsFilePath = m_applicationPath + "/settings/settings.xml";

		QFile xmlFile(settingsFilePath);

		if (!xmlFile.open(QFile::ReadOnly | QFile::Text))
		{
			return false;
		}

		QXmlStreamReader reader(&xmlFile);

		if (reader.readNextStartElement())
		{
			if (reader.name() == "settings")
			{
				while (reader.readNextStartElement())
				{
					if (reader.name() == "port")
					{
						QString port = reader.readElementText();
						m_port = port.toInt();
					}
					else if (reader.name() == "ip")
					{
						QString ip = reader.readElementText();
						m_ip = ip.toStdString();
					}
					else
					{
						reader.skipCurrentElement();
					}
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		return true;

	}

	void TCPServer::SaveMessageHistoryOfClient(SOCKET client)
	{
		PacketType packet;
		RecievePacket(client, packet);
		std::string clientName;

		if (packet == P_ChatMessage && RecieveMessage(client, clientName))
		{
			RecievePacket(client, packet);

			//Receive ten messages
			std::string clientMessages;
			if (packet == P_ChatMessage && RecieveMessage(client, clientMessages))
			{
				//Find id of client 
				int clientID = DB::GetInstance().GetClientIDByName(clientName);

				if (clientID != -1)
				{
					std::cout << clientID << std::endl;
				}

			}
		}
	}

	bool TCPServer::SendFrameMultipleMode(SOCKET client, std::vector<uchar> data, std::string userName)
	{
		PacketType packet = P_FrameMultipleMessage;

		int resultPacket = send(client, (char*)&packet, sizeof(packet), NULL);
		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Don't send a packet message" << GetLastError() << std::endl;
			return false;
		}

		int imgSize = static_cast<int>(data.size());
		std::string message = std::to_string(imgSize) + ';' + userName;

		int messageSize = static_cast<int>(message.size());
		int resultInt = send(client, (char*)&messageSize, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Don't send int for client" << GetLastError() << std::endl;
			return false;
		}


		int resultMessage = send(client, message.c_str(), messageSize , NULL);

		if (resultMessage == SOCKET_ERROR)
		{
			std::cerr << "Don't send string message for client" << GetLastError() << std::endl;
			return false;
		}

		int result = send(client, (char*)data.data(), imgSize, NULL);

		if (result == SOCKET_ERROR)
		{
			std::cerr << "Don't send message for client" << GetLastError() << std::endl;
			return false;
		}

		return true;
	}

	TCPServer::~TCPServer()
	{

	}



	bool TCPServer::ProcessPacket(SOCKET client, PacketType & packet, std::string userName)
	{
		int resultPacket = recv(client, (char*)&packet, sizeof(packet), NULL);

		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Error: process packet!" << GetLastError() << std::endl;
			return false;
		}

		std::vector<uchar> data;
		std::string message;
		char *audio;
		int length = 0;

		switch (packet)
		{
		case P_ChatMessage:
		{
			this->RecieveMessage(client, message);
			{
				std::lock_guard<std::mutex> lock(m_mutex);

				for (size_t i = 0; i < m_clients.size(); i++)
				{
					if (m_clients[i] == client)
					{
						continue;
					}
					else if (!this->SendMessage(m_clients[i], message))
					{
						break;
					}
				}
			}

			break;
		}
		case P_FrameMessage:
		{
			this->RecieveFrame(client, data);

			{
				std::lock_guard<std::mutex> lock(m_mutex);

				for (size_t i = 0; i < m_clients.size(); ++i)
				{
					auto currentClient = m_clients[i];

					if (currentClient == client)
					{
						continue;
					}
					else if (!this->SendFrame(currentClient, data))
					{
						break;
					}
				}
			}

			break;
		}

		case P_AudioMessage:
		{
			if (this->ReceiveAudio(client, &audio, length))
			{
				std::lock_guard<std::mutex> lock(m_mutex);

				for (size_t i = 0; i < m_clients.size(); i++)
				{
					if (m_clients[i] == client)
					{
						continue;
					}

					this->SendAudio(m_clients[i], audio, length);

				}

				delete[]audio;
			}

			break;
		}

		case P_InformationMessage:
		{
			if (!this->ProcessInformationMessage(client))
			{
				return false;
			}
			break;
		}

		case P_FrameMultipleMessage:
		{
			this->RecieveFrame(client, data);

			{
				std::lock_guard<std::mutex> lock(m_mutex);

				for (size_t i = 0; i < m_clients.size(); ++i)
				{
					auto currentClient = m_clients[i];

					if (currentClient == client)
					{
						continue;
					}
					else if (!this->SendFrameMultipleMode(currentClient, data, userName))
					{
						break;
					}
				}
			}

			break;
		}
		default:
			std::cout << "Undefined packet!" << std::endl;
			return false;
		}

		return true;
	}

	void TCPServer::InitializeWSA()
	{
		WORD version = MAKEWORD(2, 1);

		if (WSAStartup(version, &m_wsaData) != 0)
		{
			std::cerr << "WSAStartup does not word" << GetLastError();
			exit(1);
		}
	}


	void TCPServer::CreateBindListenSocket()
	{
		m_listenSocket = socket(AF_INET, SOCK_STREAM, NULL);

		if (m_listenSocket == SOCKET_ERROR)
		{
			std::cerr << "Can't create a socket" << GetLastError();
			exit(1);
		}

		if (bind(m_listenSocket, (SOCKADDR*)&m_addr, sizeof(m_addr)) == SOCKET_ERROR)
		{
			std::cerr << "Can't bind with addres" << GetLastError();
			exit(1);
		}

		listen(m_listenSocket, SOMAXCONN);

	}

	bool TCPServer::SendMessage(SOCKET client, const std::string message)
	{
		PacketType packet = P_ChatMessage;

		int resultPacket = send(client, (char*)&packet, sizeof(packet), NULL);
		if (resultPacket == SOCKET_ERROR)
		{
			std::cerr << "Don't send a packet message" << GetLastError() << std::endl;
			return false;
		}

		int messageSize = static_cast<int>(message.size());
		int resultInt = send(client, (char*)&messageSize, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Don't send int message for client" << GetLastError() << std::endl;
			return false;
		}

		int result = send(client, message.c_str(), messageSize, NULL);

		if (result == SOCKET_ERROR)
		{
			std::cerr << "Don't send message for client" << GetLastError() << std::endl;
			return false;
		}

		return true;
	}

	void TCPServer::SendAllMessage(const std::string message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (auto client : m_clients)
		{
			if (!this->SendMessage(client, message))
			{
				break;
			}
		}
	}

	bool TCPServer::RecieveMessage(SOCKET client, std::string & message)
	{
		int message_size = 0;

		int resultInt = recv(client, (char*)&message_size, sizeof(int), NULL);

		if (resultInt == SOCKET_ERROR)
		{
			std::cerr << "Client disconnect " << GetLastError() << std::endl;
			return false;
		}

		char *buffer = new char[message_size + 1];
		buffer[message_size] = '\0';

		int result = recv(client, buffer, message_size, NULL);

		if (result == SOCKET_ERROR)
		{
			std::cerr << "Client disconnect " << GetLastError() << std::endl;
			return false;
		}
		message = buffer;

		delete[]buffer;

		return true;
	}

	void TCPServer::DeleteClient(SOCKET client)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		auto clientIt = std::find(m_clients.begin(), m_clients.end(), client);

		if (clientIt != m_clients.end())
		{
			int positionInt = clientIt - m_clients.begin();

			auto nameIt = m_names.begin() + positionInt;

			//delete from db
			DB::GetInstance().DeleteClient(m_names[positionInt]);

			//Delete socket
			m_clients.erase(clientIt);

			//Delete name
			m_names.erase(nameIt);
		}


		return;
	}

	std::function<void(int)> TCPServer::CreateHandler()
	{
		return  [this](int clientNumber)
		{
			SOCKET client = 0;
			std::string clientName;

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				client = m_clients[clientNumber];
				clientName = m_names[clientNumber];
			}

			//Create VideoThread

			while (true)
			{
				PacketType packet;

				if (!this->ProcessPacket(client, packet, clientName))
				{
					std::cout << "Close Thread!" << std::endl;

					this->DeleteClient(client);

					::closesocket(client);

					this->SendAllMessage(clientName + " is disconnected!");

					this->SendClientsList();

					std::cout << "Video counter = " << m_videoCounter << std::endl;

					return;
				}

				std::this_thread::sleep_for(std::chrono::microseconds(5));
			}
		};
	}
}