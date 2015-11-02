#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <string.h>
#include <string>
#include <cstdlib>
#include "manager.h"
#include <dirent.h>
#include <sys/epoll.h>
#include "tools.h"
#include <fstream>
#include <sys/time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <assert.h>

using namespace std;

Manager::Manager(string configfile)
{
	m_iPid = 0;
	m_pSocket = NULL;
	m_pClientSock = NULL;
	m_semRequest = NULL;
	m_mtxRequest = NULL;
	m_pUserProcess = NULL;
	m_strFileDir = "";
	
	m_mtxRecv = NULL;
	m_mtxSend = NULL;

	m_iPutTime = 0;
	m_iGetTime = 0;
	m_iDelTime = 0;

	Config* config = Config::Instance();
	if( config->ParseConfig(configfile.c_str(), "SYSTEM") != 0)
	{
		cout<<"parse config file:["<<configfile<<"] failed, exit"<<endl;
		exit(-1);
	}


	m_iServernum = config->GetIntVal("SYSTEM", "servernum", 8);
	m_iCurrentServernum = config->GetIntVal("SYSTEM", "currentservernum", 0);
	assert(m_iCurrentServernum < m_iServernum);
	m_ihashnum = config->GetIntVal("SYSTEM", "hashnum", 100000);
	m_iPeerThreadPoolNum = config->GetIntVal("SYSTEM", "threadnum", 5);
	m_iTestMode = config->GetIntVal("SYSTEM", "testmode", 0);

	m_htm.Create((unsigned int)m_ihashnum);

	for(int i = 0; i < m_iServernum; i++)
	{
		char serverip[30] = {0};
		char serverport[30] = {0};
		char fileserverport[30] = {0};
		char serveridentifier[30] = {0};
		char filebufdir[30] = {0};
		char downloaddir[30] = {0};
		snprintf(serverip, 30, "serverip_%d", i);
		snprintf(serverport, 30, "serverport_%d", i);
		snprintf(fileserverport, 30, "fileserverport_%d", i);
		snprintf(serveridentifier, 30, "server_identifier_%d", i);
		snprintf(filebufdir, 30, "filebufdir_%d", i);
		snprintf(downloaddir, 30, "downloaddir_%d", i);
		PeerInfo pi;
		pi.ip = config->GetStrVal("SYSTEM", serverip, "0.0.0.0");
		pi.port = config->GetIntVal("SYSTEM", serverport, 55555);
		pi.fileserverport = config->GetIntVal("SYSTEM", fileserverport, 70000);
		pi.identifier = config->GetIntVal("SYSTEM", serveridentifier, 0);
		pi.filebufdir = config->GetStrVal("SYSTEM", filebufdir, "./file/");
		pi.downloaddir = config->GetStrVal("SYSTEM", downloaddir, "./download/");
		pi.sock = NULL;

		m_vecPeerInfo.push_back(pi);
	}

	m_strSelfIp = m_vecPeerInfo[m_iCurrentServernum].ip;
	m_iSelfPort = m_vecPeerInfo[m_iCurrentServernum].port;
	m_strPeerFileBufferDir = m_vecPeerInfo[m_iCurrentServernum].filebufdir;

}

Manager::~Manager()
{
	if(m_pClientSock)
	{
		delete m_pClientSock;
		m_pClientSock = NULL;
	}
	if(m_pSocket)
	{
		delete m_pSocket;
		m_pSocket = NULL;
	}
	if(m_semRequest)
	{
		delete m_semRequest;
		m_semRequest = NULL;
	}
	if(m_mtxRequest)
	{
		delete m_mtxRequest;
		m_mtxRequest = NULL;
	}
	if(m_mtxRecv)
	{
		delete m_mtxRecv;
		m_mtxRecv = NULL;
	}
	if(m_mtxSend)
	{
		delete m_mtxSend;
		m_mtxSend = NULL;
	}
	for(unsigned int i = 0; i < m_vecProcessThread.size(); i++)
	{
		if(m_vecProcessThread[i])
		{
			delete m_vecProcessThread[i];
			m_vecProcessThread[i] = NULL;
		}
	}
	if(m_pUserProcess)
	{
		delete m_pUserProcess;
		m_pUserProcess = NULL;
	}

	for(unsigned int i = 0; i < m_vecPeerInfo.size(); i++)
	{
		if(m_vecPeerInfo[i].sock != NULL)
		{
			m_vecPeerInfo[i].sock->Close();
			delete m_vecPeerInfo[i].sock;
			m_vecPeerInfo[i].sock = NULL;
		}
	}
}

int Manager::Start()
{
	m_iPid = fork();
	if(m_iPid == -1)
	{
		cout<<"fork failed"<<endl;
		return -1;
	}
	else if (m_iPid == 0)
	{
		if(Init() < 0)
		{
			cout<<"manager init failed"<<endl;
			return 0;
		}
		if(Listen() < 0)
		{
			cout<<"manager listen failed"<<endl;
			return -1;
		}
		Loop();
	}
	return 0;
}

int  Manager::IsStoped()
{
	int result = ::kill(m_iPid, 0);
	if (0 == result || errno != ESRCH)
	{
		return false;
	}
	else	
	{
		m_iPid = 0;
		return true;
	}
}

int Manager::Init()
{
	m_pSocket = new Socket(m_strSelfIp.c_str(), m_iSelfPort, ST_TCP);
	m_semRequest = new Sem(0, 0);
	m_mtxRequest = new Mutex();
	m_mtxRecv = new Mutex();
	m_mtxSend = new Mutex();
	for(int i = 0; i <= m_iPeerThreadPoolNum; i++)
	{
		Thread* thread = new Thread(Process, this);
		m_vecProcessThread.push_back(thread);
	}

	m_pUserProcess = new Thread(UserCmdProcess, this);

	return 0;
}

int Manager::Listen()
{
	if (m_pSocket->Create() < 0)
	{
		cout<<"socket create failed"<<endl;
		return -1;
	}
	if(m_pSocket->SetSockAddressReuse(true) < 0)
		cout<<"set socket address reuse failed"<<endl;
	if(m_pSocket->Bind() < 0)
	{
		cout<<"socket bind failed"<<endl;
		return -1;
	}
	if(m_pSocket->Listen() < 0)
	{
		cout<<"socket listen failed"<<endl;
		return -1;
	}
	cout<<"Peer server now begin listen"<<endl;
	return 0;
}

int Manager::Loop()
{
	int listenfd = m_pSocket->GetSocket();
	struct epoll_event ev, events[MAX_EPOLL_FD];
	int epfd = epoll_create(MAX_EPOLL_FD);
	ev.data.fd = listenfd;
	ev.events = EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
	while(1)
	{
		int nfds = epoll_wait(epfd, events, MAX_EPOLL_FD, -1);
		if(nfds <= 0) continue;
		for (int i = 0; i < nfds; i++)
		{
			if(events[i].data.fd == listenfd)
			{
				Socket* s = new Socket();
				int iRet = m_pSocket->Accept(s);
				if(iRet < 0)
				{
					cout<<"socket accept failed: ["<<errno<<"]"<<endl;
					continue;
				}
				s->SetSockAddressReuse(true);
				m_vecServerSock.push_back(s);
				// all the new socket into the listen queue to resue connection
				struct epoll_event ev;
				ev.data.fd = s->GetSocket();
				ev.events = EPOLLIN|EPOLLERR;
				epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
				m_mtxRequest->Lock();
				m_rq.push(s);
				m_semRequest->Post();
				m_mtxRequest->Unlock();				
			}
			else if(events[i].events & EPOLLIN)
			{
				Socket* s = NULL;
				for(unsigned int j = 0; j < m_vecServerSock.size(); j++)
				{
					if(events[i].data.fd == m_vecServerSock[j]->GetSocket())
					{
						s = m_vecServerSock[j];
						break;
					}
				}
				if(s == NULL)
					cout<<"event id["<<events[i].events<<"]"<<endl;
				assert(s != NULL);
				m_mtxRequest->Lock();
				m_rq.push(s);
				m_semRequest->Post();
				m_mtxRequest->Unlock();	
			}
			else if(events[i].events & EPOLLERR)
			{
				cout<<"socket get error epoll"<<endl;
			}

		}
		usleep(0);
	}
return 0;
}

void* Process(void* arg)
{
	Manager* pmgr = (Manager*)arg;

	while(1)
	{
		pmgr->m_semRequest->Wait();
		pmgr->m_mtxRequest->Lock();
		Socket* client = pmgr->m_rq.front();
		pmgr->m_rq.pop();
		pmgr->m_mtxRequest->Unlock();

		char* recvBuff = new char[MAX_MESSAGE_LENGTH];
		bzero(recvBuff, MAX_MESSAGE_LENGTH);
		pmgr->m_mtxRecv->Lock();
		if(client->Recv(recvBuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
		{
			pmgr->m_mtxRecv->Unlock();
			delete [] recvBuff;
			continue;
		}

		char* sendBuff = new char[MAX_MESSAGE_LENGTH];
		bzero(sendBuff, MAX_MESSAGE_LENGTH);
		Message* sendMsg = (Message*)sendBuff;

		Message* recvMsg = (Message*)recvBuff;
		cout<<"action "<<recvMsg->action<<" file: "<<recvMsg->key<<" identifier "<<recvMsg->value<<" filelen:"<<recvMsg->msglength<<endl;
		switch(recvMsg->action)
		{
			case CMD_SEARCH:
			{
				pmgr->m_mtxRecv->Unlock();
				string value = "";
				if(pmgr->m_htm.Search(recvMsg->key, value) != 0)
				{
					// search failed, return the value null
					sendMsg->action = CMD_OK;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					bzero(sendMsg->value, MAX_VALUE_LENGTH);
				}
				else
				{
					sendMsg->action = CMD_OK;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					strncpy(sendMsg->value, value.c_str(), MAX_VALUE_LENGTH);
				}
				//pmgr->m_mtxSend->Lock();
				client->Send(sendMsg, MAX_MESSAGE_LENGTH);
				//pmgr->m_mtxSend->Unlock();
				break;
			}

			case CMD_PUT:
			{
				pmgr->m_mtxRecv->Unlock();
				if(pmgr->m_htm.Insert(recvMsg->key, recvMsg->value) != 0)
				{
					sendMsg->action = CMD_FAILED;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					strncpy(sendMsg->value, recvMsg->value, MAX_VALUE_LENGTH);
				}
				else
				{
					sendMsg->action = CMD_OK;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					strncpy(sendMsg->value, recvMsg->value, MAX_VALUE_LENGTH);
				}
				//pmgr->m_mtxSend->Lock();
				client->Send(sendMsg, MAX_MESSAGE_LENGTH);
				//pmgr->m_mtxSend->Unlock();
				break;				
			}
			case CMD_DEL:
			{
				pmgr->m_mtxRecv->Unlock();
				if(pmgr->m_htm.Delete(recvMsg->key) != 0)
				{
					sendMsg->action = CMD_FAILED;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					bzero(sendMsg->value, MAX_VALUE_LENGTH);
				}
				else
				{
					sendMsg->action = CMD_OK;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					bzero(sendMsg->value, MAX_VALUE_LENGTH);
				}
				//pmgr->m_mtxSend->Lock();
				client->Send(sendMsg, MAX_MESSAGE_LENGTH);
				//pmgr->m_mtxSend->Unlock();
				break;				
			}
			case CMD_DOWNLOAD:
			{
				pmgr->m_mtxRecv->Unlock();
				string downloadFilename = recvMsg->key;
				cout<<"request to download file["<<downloadFilename<<"]"<<endl;
				string dirname = "";
				DIR* dir = NULL;
				struct dirent* direntry;

				if((dir = opendir(pmgr->m_strPeerFileBufferDir.c_str())) != NULL)
				{
					bool findDownloadFile = false;
					while((direntry = readdir(dir)) != NULL)
					{
						string fn = direntry->d_name;
						if(fn == downloadFilename)
						{
							findDownloadFile = true;
							break;
						}
					}
					if(findDownloadFile)
					{
						char filepath [100] = {0};
						strncpy(filepath, pmgr->m_strPeerFileBufferDir.c_str(), 99);
						strncpy(filepath+strlen(pmgr->m_strPeerFileBufferDir.c_str()), downloadFilename.c_str(), 99 - strlen(pmgr->m_strPeerFileBufferDir.c_str()));
						ifstream istream (filepath, std::ifstream::binary);
						istream.seekg(0, istream.end);
						int fileLen = istream.tellg();
						istream.seekg(0, istream.beg);
						char* buffer = new char[fileLen + sizeof(MsgPkg)];
						MsgPkg* msg = (MsgPkg*)buffer;
						msg->msgcmd = MSG_CMD_DOWNLOAD_RESPONSE;
						msg->msglength = fileLen;
						cout<<"file len["<<fileLen<<"]"<<endl;
						istream.read(buffer + sizeof(MsgPkg), fileLen);
						istream.close();

						client->Send(buffer, fileLen+sizeof(MsgPkg));
						delete [] buffer;
					}
					else
					{
						char buffer[MSG_HEAD_LEN] = {0};
						MsgPkg* msg = (MsgPkg*)buffer;
						msg->msgcmd = MSG_CMD_DOWNLOAD_RESPONSE;
						msg->msglength = -1;
						client->Send(buffer, MSG_HEAD_LEN);
					}
				}
				else
				{
					char buffer[MSG_HEAD_LEN] = {0};
					MsgPkg* msg = (MsgPkg*)buffer;
					msg->msgcmd = MSG_CMD_DOWNLOAD_RESPONSE;
					msg->msglength = -1;
					client->Send(buffer, MSG_HEAD_LEN);
				}
				break;	
			}
			case CMD_REPLICA:
			{
				// write to file
				char* file = new char[recvMsg->msglength];
				if(client->Recv(file, recvMsg->msglength) != recvMsg->msglength)
				{
					pmgr->m_mtxRecv->Unlock();
					cout<<"replica file recv failed"<<endl;
					delete [] file;
					sendMsg->action = CMD_FAILED;
					client->Send(sendMsg, MAX_MESSAGE_LENGTH);					
					continue;
				}

				pmgr->m_mtxRecv->Unlock();
				ofstream out;
				string filefullpath = pmgr->m_vecPeerInfo[pmgr->m_iCurrentServernum].filebufdir + recvMsg->key;
				out.open(filefullpath.c_str(), ios::out|ios::binary);
				out.write(file, recvMsg->msglength);
				out.close();
				delete [] file;

				// record the key, value which is self.
				if(pmgr->m_htm.Insert(recvMsg->key, pmgr->MakeIdentifer()) != 0)
				{
					sendMsg->action = CMD_FAILED;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					strncpy(sendMsg->value, recvMsg->value, MAX_VALUE_LENGTH);
				}
				else
				{
					sendMsg->action = CMD_OK;
					strncpy(sendMsg->key, recvMsg->key, MAX_KEY_LENGTH);
					strncpy(sendMsg->value, recvMsg->value, MAX_VALUE_LENGTH);
				}
				client->Send(sendMsg, MAX_MESSAGE_LENGTH);

				break;
			}
			default:
			cout<<"cmd unknown["<<recvMsg->action<<"]"<<endl;
		}

		delete[] recvBuff;
		delete[] sendBuff;
	}

	return 0;
}


void* UserCmdProcess(void* arg)
{
	Manager* pmgr = (Manager*)arg;

	cout<<"Welcome to the distributed file sharing system, you are in the peer client"<<endl;
	if(pmgr->m_iTestMode != 0)
		pmgr->testmode();

	cout<<endl<<"Welcome to the user interface"<<endl;
	cout<<"You can put, get, key to and from the hash index system"<<endl;

	while(1)
	{
		if(pmgr->Register() != 0)
		{
			cout<<"Register failed. Wanna try again please press y"<<endl;
			char respond;
			cin>>respond;
			if(respond == 'y' || respond == 'Y')
				continue;
			else
				break;
		}
		else break;
	}

	while(1)
	{
		cout<<"Do now replica all files?"<<endl;
		char cmd;
		cin>>cmd;
		if(cmd == 'y' || cmd == 'Y')
			pmgr->ReplicaFile();

		break;
	}

	while(1)
	{
		string filename = "";
		string value = "";
		cout<<"Please enter the file name you wanna download"<<endl;
		cin>>filename;
		if(pmgr->get(filename, value) != 0 || value == "")
		{
			cout<<"get failed"<<endl;
			continue;
		}
		else
			cout<<"get success"<<endl;
		string ip = "";
		int port = 0;
		pmgr->ParserIdentifer(value, ip, port);
		cout<<"Now downlaod this file?"<<endl;
		char isDownload;
		cin>>isDownload;
		if(isDownload == 'y' || isDownload == 'Y')
			pmgr->DownloadFile(filename, ip, port);
	}
	return 0;
}

Socket* Manager::getSock(string ip, int port)
{
	Socket** s = 0;
	for(unsigned int i = 0; i < m_vecPeerInfo.size(); i++)
	{
		if(m_vecPeerInfo[i].ip == ip && m_vecPeerInfo[i].port == port)
		{
			if(m_vecPeerInfo[i].sock != NULL)
				return m_vecPeerInfo[i].sock;
			else
				s = &m_vecPeerInfo[i].sock;
		}
	}

	Socket* sock = new Socket(ip.c_str(), port, ST_TCP);
	sock->Create();

	if(sock->Connect() != 0)
	{
		cout<<"connect to hash server failed"<<endl;
		delete sock;
		return 0;
	}

	*s = sock;
	return sock;
}



int Manager::put(string key, string value)
{
	int hash = getHash(key);
	string severip = m_vecPeerInfo[hash%m_iServernum].ip;
	int serverport = m_vecPeerInfo[hash%m_iServernum].port;
	Socket* sock = NULL;
	if((sock = getSock(severip.c_str(), serverport)) == NULL)
	{
		cout<<"get sock failed"<<endl;
		return -1;
	}

	char* sbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(sbuff, MAX_MESSAGE_LENGTH);
	Message* smsg = (Message*)sbuff;
	smsg->action = CMD_PUT;
	strncpy(smsg->key, key.c_str(), MAX_KEY_LENGTH);
	strncpy(smsg->value, value.c_str(), MAX_VALUE_LENGTH);

	if(sock->Send(sbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"send put message to hash server failed"<<endl;
		delete[] sbuff;
		return -1;
	}

	char* rbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(rbuff, MAX_MESSAGE_LENGTH);
	if(sock->Recv(rbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"put message recv from hash server failed"<<endl;
		delete[] sbuff;
		delete[] rbuff;
		return -1;
	}

	Message* rmsg = (Message*)rbuff;
	int ret = rmsg->action == CMD_OK?0:-1;

	delete[] sbuff;
	delete[] rbuff;
	return ret;
}
int Manager::get(string key, string& value)
{
	int hash = getHash(key);
	string severip = m_vecPeerInfo[hash%m_iServernum].ip;
	int serverport = m_vecPeerInfo[hash%m_iServernum].port;
	Socket* sock = NULL;
	if((sock = getSock(severip.c_str(), serverport)) == NULL)
	{
		cout<<"get sock failed"<<endl;
		return -1;
	}

	char* sbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(sbuff, MAX_MESSAGE_LENGTH);
	Message* smsg = (Message*)sbuff;
	smsg->action = CMD_SEARCH;
	strncpy(smsg->key, key.c_str(), MAX_KEY_LENGTH);

	if(sock->Send(sbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"send search message to hash server failed"<<endl;
		delete[] sbuff;
		return -1;
	}

	char* rbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(rbuff, MAX_MESSAGE_LENGTH);
	if(sock->Recv(rbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"search message recv from hash server failed"<<endl;
		delete[] sbuff;
		delete[] rbuff;
		return -1;
	}

	Message* rmsg = (Message*)rbuff;
	value = rmsg->value;

	delete[] sbuff;
	delete[] rbuff;
	return 0;
}

bool Manager::del(string key)
{
	int hash = getHash(key);
	string severip = m_vecPeerInfo[hash%m_iServernum].ip;
	int serverport = m_vecPeerInfo[hash%m_iServernum].port;
	Socket* sock = NULL;
	if((sock = getSock(severip.c_str(), serverport)) == NULL)
	{
		cout<<"get sock failed"<<endl;
		return -1;
	}

	char* sbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(sbuff, MAX_MESSAGE_LENGTH);
	Message* smsg = (Message*)sbuff;
	smsg->action = CMD_DEL;
	strncpy(smsg->key, key.c_str(), MAX_KEY_LENGTH);

	if(sock->Send(sbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"send put message to hash server failed"<<endl;
		delete[] sbuff;
		return -1;
	}

	char* rbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(rbuff, MAX_MESSAGE_LENGTH);
	if(sock->Recv(rbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"put message recv from hash server failed"<<endl;
		delete[] sbuff;
		delete[] rbuff;
		return -1;
	}

	Message* rmsg = (Message*)rbuff;
	int ret = rmsg->action == CMD_OK?0:-1;

	delete[] sbuff;
	delete[] rbuff;
	return ret;
}

int Manager::getHash(const string& key)
{
	int total = 0;
	for(unsigned int i = 0; i < key.length(); i++)
		total += key[i];
	return total;
}

int Manager::testmode()
{
	return 0;
}

string Manager::MakeIdentifer()
{
	return m_strSelfIp + ";" + iTo4ByteString(m_iSelfPort);
}

int Manager::ParserIdentifer(const string& identifer, string& ip, int& port)
{
	int pos = identifer.find(";");
	ip = identifer.substr(0, pos);
	port = atoi(identifer.substr(pos+1).c_str());
	return 0;
}

int Manager::Register()
{
	string identifer = MakeIdentifer();
	string dirname = "";
	DIR* dir = NULL;
	struct dirent* direntry;

	cout<<"please input the directory to register"<<endl;
	cin >> dirname;
	if((dir = opendir(dirname.c_str())) == NULL)
	{
		cout<<"Sorry! can not open this directory: "<<dirname<<endl;
		return -1;
	}
	m_strFileDir = dirname;
	while((direntry = readdir(dir)) != NULL)
	{
		if(direntry -> d_type != DT_REG)
			continue;
		string nametemp = direntry->d_name;
		if(put(nametemp, identifer) != 0)
		{
			cout<<"put file["<<nametemp<<"]failed"<<endl;
			return -1;
		}
	}

	return 0;
}

int Manager::DownloadFile(string filename, string ip, int port)
{
	Socket* sock = NULL;
	if((sock = getSock(ip, port)) == NULL)
	{
		cout<<"get sock failed"<<endl;
		return -1;
	}

	char* sbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(sbuff, MAX_MESSAGE_LENGTH);
	Message* smsg = (Message*)sbuff;
	smsg->action = CMD_DOWNLOAD;
	strncpy(smsg->key, filename.c_str(), MAX_KEY_LENGTH);

	if(sock->Send(sbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"send put message to file download server failed"<<endl;
		delete[] sbuff;
		return -1;
	}

	delete[] sbuff;

	char szBuffer[MSG_HEAD_LEN] = {0};
	if(sock->Recv(szBuffer, MSG_HEAD_LEN) != MSG_HEAD_LEN)
	{
		cout<<"downlaod socket recv failed"<<endl;
		return -1;
	}
	MsgPkg* msg = (MsgPkg*)szBuffer;
	
	if(msg->msglength < 0)
	{
		cout<<"peer server ip: "<<ip<<" port: "<<port<<" does not contain file: "<< filename<<endl;
		return 0;
	}
	else
	{
		char* file = new char[msg->msglength];
		if(sock->Recv(file, msg->msglength) != msg->msglength)
		{
			cout<<"download recv failed"<<endl;
			delete [] file;
			return -1;
		}

		ofstream out;
		string filefullpath = m_vecPeerInfo[m_iCurrentServernum].downloaddir + filename;
		out.open(filefullpath.c_str(), ios::out|ios::binary);
		out.write(file, msg->msglength);
		out.close();
		delete [] file;
	}

	return 0;
}

int Manager::DoReplica(string filepath, string filename, int serverindex)
{
	string identifer = MakeIdentifer();
	string severip = m_vecPeerInfo[serverindex%m_iServernum].ip;
	int serverport = m_vecPeerInfo[serverindex%m_iServernum].port;
	Socket* sock = NULL;
	if((sock = getSock(severip.c_str(), serverport)) == NULL)
	{
		cout<<"get sock failed"<<endl;
		return -1;
	}

	ifstream istream (filepath.c_str(), std::ifstream::binary);
	istream.seekg(0, istream.end);
	int fileLen = istream.tellg();
	istream.seekg(0, istream.beg);
	char* buffer = new char[fileLen + sizeof(Message)];
	bzero(buffer, fileLen+sizeof(Message));	
	Message* msg = (Message*)buffer;
	msg->action = CMD_REPLICA;
	strncpy(msg->key, filename.c_str(), MAX_KEY_LENGTH);
	strncpy(msg->value, identifer.c_str(), MAX_VALUE_LENGTH);
	msg->msglength = fileLen;
	cout<<"file len["<<fileLen<<"]"<<endl;
	istream.read(buffer + sizeof(Message), fileLen);
	istream.close();

	cout<<"action "<<msg->action<<" filename " <<msg->key<<" filelen "<<msg->msglength<<endl;
	sock->Send(buffer, fileLen+sizeof(Message));


	char* rbuff = new char[MAX_MESSAGE_LENGTH];
	bzero(rbuff, MAX_MESSAGE_LENGTH);
	if(sock->Recv(rbuff, MAX_MESSAGE_LENGTH) != MAX_MESSAGE_LENGTH)
	{
		cout<<"replica recv from server failed"<<endl;
		delete[] buffer;
		delete[] rbuff;
		return -1;
	}

	Message* rmsg = (Message*)rbuff;
	int ret = rmsg->action == CMD_OK?0:-1;

	delete[] buffer;
	delete[] rbuff;
	return ret;
}

int Manager::ReplicaFile()
{
	DIR* dir = NULL;
	struct dirent* direntry;

	if((dir = opendir(m_strFileDir.c_str())) == NULL)
	{
		cout<<"Sorry! can not open this directory: "<<m_strFileDir<<endl;
		return -1;
	}
	while((direntry = readdir(dir)) != NULL)
	{
		if(direntry -> d_type != DT_REG)
			continue;
		string nametemp = direntry->d_name;
		int index = getHash(nametemp) + 1;
		if(index%m_iServernum == m_iCurrentServernum)
			index++;
		if(DoReplica(m_strFileDir+nametemp, nametemp, index) != 0)
		{
			cout<<"Duplica file["<<nametemp<<"]failed"<<endl;
			return -1;
		}
		index++;
		if(index%m_iServernum == m_iCurrentServernum)
			index++;
		if(DoReplica(m_strFileDir+nametemp, nametemp, index) != 0)
		{
			cout<<"Duplica file["<<nametemp<<"]failed"<<endl;
			return -1;
		}
	}
	cout<<"Duplica finished"<<endl;
	return 0;
}


























