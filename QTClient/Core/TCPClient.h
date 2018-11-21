#pragma  once
#pragma comment(lib,"ws2_32.lib")
#include <iostream>
#include <WinSock2.h>
#include <string>
#include <functional>
#include <QObject>
#include <QString>
#include <mutex>
#include <tuple>
#include <opencv2/opencv.hpp>
#include <condition_variable>


class FaceDetector;

class TCPClient: public QObject{
	Q_OBJECT
public:
	enum class PacketType {
		P_ChatMessage,
		P_FrameMessage,
		P_AudioMessage,
		P_InformationMessage
	};
private:

	bool ProcessPacket(PacketType &packet);
	void InitializeWSA();
	std::function<void (void)> CreateProcessHandler();
	void CreateSocket();
	void RecieveFrame();
	void RecieveAudio();
	void RecieveMessage();
	void RecieveInformationMessage(std::string &message);
	void ReceiveMessage(std::string &message);
	void CreateFaceDetector(QString path);
	void ProcessFrameThread();

public:
	explicit TCPClient(int port, const char *ip,std::string name);
	~TCPClient();
	bool Connect();

	void SendMessageWithoutName(std::string message);
	void SendMessage(std::string message);
	void SendFrame(std::vector<uchar> buffer);
	void SendAudio(QByteArray buffer,int lengt);
	void SendInformationMessage(std::string message);


	std::tuple<std::string, std::string, int> GetClientInformation() const;
	cv::Mat GetCurrentFrame() const;

	void SetAppPath(QString path);
	void StopThread();
	bool IsSameName();
	
Q_SIGNALS:
	void recieveEventMessage(QString message);
	void recieveEventFrame();
	void recieveEventAudio(QByteArray,int);

	void startShowVideo();
	void stopShowVideo();

	void updateList(QString);
private:
	QString m_path;
	WSADATA m_wsaData;
	SOCKADDR_IN m_addr;
	SOCKET m_connection;

	int m_port;
	cv::Mat m_currentFrame;
	std::string m_name;
	std::string m_ip;

	std::thread m_frameThread;
	std::mutex m_mutex;
	std::mutex m_frameMutex;
	std::condition_variable m_cv;

	bool m_frameReady = true;
	bool m_sameName = false;
	bool m_terminating;

	std::unique_ptr<FaceDetector> m_faceDetector;
};