#ifndef MODEL_H
#define MODEL_H

#include <string>
#include <vector>
#include "socket.h"
using namespace std;

const int MAX_EPOLL_FD = 30;
const char SPLIT_FILE = ' ';


enum Cmd_Indicate
{
	CMD_OK = 0,
	CMD_FAILED = 1,
	CMD_SEARCH = 2,
	CMD_PUT = 3,
	CMD_DEL = 4,
	CMD_DOWNLOAD = 5,
	MSG_CMD_DOWNLOAD_RESPONSE = 6,
	CMD_REPLICA = 7,
	CMD_UNKNOWN = 8
};

struct FilePeer
{
	string filename;
	string identifier;
};

const int MAX_INDICATE_LENGTH = sizeof(Cmd_Indicate);
const int MAX_KEY_LENGTH = 100;
const int MAX_VALUE_LENGTH = 1000;
//const int MAX_MESSAGE_LENGTH = MAX_KEY_LENGTH + MAX_VALUE_LENGTH + MAX_INDICATE_LENGTH;
typedef struct PeerInfo
{
	string ip;
	int port;
	int fileserverport;
	int identifier;
	string filebufdir;
	string downloaddir;
	Socket* sock;
	bool sockAvailable;

}PeerInfo;

#pragma pack(1)
typedef struct Message
{
	Cmd_Indicate action;
	int msglength;
	char key[MAX_KEY_LENGTH];
	char value[MAX_VALUE_LENGTH];
}Message;

struct DownloadPkg
{
	char file[1];
};

struct MsgPkg
{
	Cmd_Indicate msgcmd;
	int msglength;
};
const int MSG_HEAD_LEN = sizeof(MsgPkg);
const int MAX_MESSAGE_LENGTH = sizeof(Message);

#pragma pack()

#endif
