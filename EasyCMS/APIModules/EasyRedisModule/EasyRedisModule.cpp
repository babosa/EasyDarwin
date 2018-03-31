/*
	Copyleft (c) 2012-2016 EasyDarwin.ORG.  All rights reserved.
	Github: https://github.com/EasyDarwin
	WEChat: EasyDarwin
	Website: http://www.EasyDarwin.org
*/
#include "EasyRedisModule.h"

#include "OSHeaders.h"
#include "QTSSModuleUtils.h"
#include "EasyRedisClient.h"
#include "QTSServerInterface.h"
#include "HTTPSessionInterface.h"
#include "Format.h"
#include "EasyUtil.h"
#include "Resources.h"
#include "Windows/hiredis.h"
#include <stdio.h>

// STATIC VARIABLES
static QTSS_ModulePrefsObject	modulePrefs = NULL;
static QTSS_PrefsObject			sServerPrefs = NULL;
static QTSS_ServerObject		sServer = NULL;

// Redis IP
static char*            sRedis_IP = NULL;
static char*            sDefaultRedis_IP_Addr = "127.0.0.1";
// Redis Port
static UInt16			sRedisPort = 6379;
static UInt16			sDefaultRedisPort = 6379;
// Redis password
static char*            sRedisPassword = NULL;
static char*            sDefaultRedisPassword = "EasyDSSEasyDarwinEasyCMSEasyCamera";

//static EasyRedisClient* sRedisClient = NULL;//the object pointer that package the redis operation
static bool				sIfConSucess = false;
static OSMutex			sMutex;

static redisContext*	redisContext_ = NULL;

static void RedisErrorHandler();

// FUNCTION PROTOTYPES
static QTSS_Error EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock);
static QTSS_Error Register(QTSS_Register_Params* inParams);
static QTSS_Error Initialize(QTSS_Initialize_Params* inParams);
static QTSS_Error RereadPrefs();

static bool RedisConnect();
static QTSS_Error RedisTTL();
static QTSS_Error RedisSetDevice(Easy_DeviceInfo_Params* inParams);
static QTSS_Error RedisDelDevice(Easy_DeviceInfo_Params* inParams);
static QTSS_Error RedisGetAssociatedDarwin(QTSS_GetAssociatedDarwin_Params* inParams);

class RedisReplyObjectDeleter
{
public:
	RedisReplyObjectDeleter() : fReply(NULL) {}
	RedisReplyObjectDeleter(redisReply* reply) : fReply(reply) {}
	~RedisReplyObjectDeleter()
	{
		if (fReply)
		{
			freeReplyObject(fReply);
		}
	}

	void ClearObject() { fReply = NULL; }

	void SetObject(redisReply* reply)
	{
		fReply = reply;
	}
	redisReply* GetObject() { return fReply; }

private:

	redisReply* fReply;
};

QTSS_Error EasyRedisModule_Main(void* inPrivateArgs)
{
	return _stublibrary_main(inPrivateArgs, EasyRedisModuleDispatch);
}

QTSS_Error EasyRedisModuleDispatch(QTSS_Role inRole, QTSS_RoleParamPtr inParamBlock)
{
	switch (inRole)
	{
	case QTSS_Register_Role:
		return Register(&inParamBlock->regParams);
	case QTSS_Initialize_Role:
		return Initialize(&inParamBlock->initParams);
	case QTSS_RereadPrefs_Role:
		return RereadPrefs();
	case Easy_RedisSetDevice_Role:
		return RedisSetDevice(&inParamBlock->DeviceInfoParams);
	case Easy_RedisDelDevice_Role:
		return RedisDelDevice(&inParamBlock->DeviceInfoParams);
	case Easy_RedisTTL_Role:
		return RedisTTL();
	case Easy_RedisGetEasyDarwin_Role:
		return RedisGetAssociatedDarwin(&inParamBlock->GetAssociatedDarwinParams);
	default: break;
	}
	return QTSS_NoErr;
}

QTSS_Error Register(QTSS_Register_Params* inParams)
{
	// Do role setup
	(void)QTSS_AddRole(QTSS_Initialize_Role);
	(void)QTSS_AddRole(QTSS_RereadPrefs_Role);
	(void)QTSS_AddRole(Easy_RedisTTL_Role);
	(void)QTSS_AddRole(Easy_RedisSetDevice_Role);
	(void)QTSS_AddRole(Easy_RedisDelDevice_Role);
	(void)QTSS_AddRole(Easy_RedisGetEasyDarwin_Role);

	static char* sModuleName = "EasyRedisModule";
	::strcpy(inParams->outModuleName, sModuleName);

	return QTSS_NoErr;
}

QTSS_Error Initialize(QTSS_Initialize_Params* inParams)
{
	QTSSModuleUtils::Initialize(inParams->inMessages, inParams->inServer, inParams->inErrorLogStream);
	sServer = inParams->inServer;
	sServerPrefs = inParams->inPrefs;
	modulePrefs = QTSSModuleUtils::GetModulePrefsObject(inParams->inModule);

	RereadPrefs();

	RedisConnect();

	return QTSS_NoErr;
}

QTSS_Error RereadPrefs()
{
	delete[] sRedis_IP;
	sRedis_IP = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_ip", sDefaultRedis_IP_Addr);

	QTSSModuleUtils::GetAttribute(modulePrefs, "redis_port", qtssAttrDataTypeUInt16, &sRedisPort, &sDefaultRedisPort, sizeof(sRedisPort));

	delete[] sRedisPassword;
	sRedisPassword = QTSSModuleUtils::GetStringAttribute(modulePrefs, "redis_password", sDefaultRedisPassword);

	return QTSS_NoErr;
}

bool RedisConnect()
{
	if (sIfConSucess)
	{
		return true;
	}

	bool theRet = false;
	do
	{
		struct timeval timeout = { 2, 0 }; // 2 seconds
		redisContext_ = redisConnectWithTimeout(sRedis_IP, sRedisPort, timeout);
		if (!redisContext_ || redisContext_->err)
		{
			if (redisContext_)
			{
				printf("Redis context connect error \n");
			}
			else
			{
				printf("Connection error: can't allocate redis context\n");
			}

			theRet = false;
			break;
		}

		string auth = Format("auth %s", string(sRedisPassword));
		redisReply* reply = static_cast<redisReply*>(redisCommand(redisContext_, auth.c_str()));

		RedisReplyObjectDeleter replyDeleter(reply);
		if (!reply || string(reply->str) != string("OK"))
		{
			printf("Redis auth error\n");
			theRet = false;
			break;
		}

		theRet = true;
		sIfConSucess = true;

		printf("Connect Redis success\n");

	} while (0);

	if (!theRet && redisContext_)
	{
		RedisErrorHandler();
	}

	return theRet;
}

QTSS_Error RedisTTL()
{
	OSMutexLocker mutexLock(&sMutex);

	QTSS_Error theRet = QTSS_NoErr;
	if (!RedisConnect())
	{
		return QTSS_NotConnected;
	}
	string server(QTSServerInterface::GetServer()->GetServerName().Ptr);
	string id(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
	UInt32 load = QTSServerInterface::GetServer()->GetNumServiceSessions();

	do
	{
		string expire = Format("expire %s:%s 15", server, id);
		redisReply* reply = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));

		RedisReplyObjectDeleter replyDeleter(reply);
		if (!reply)
		{
			theRet = QTSS_NotConnected;
			break;
		}

		if (reply->integer == 0)
		{
			string cmsIp(QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP());
			auto cmsPort = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANPort();
			auto hmset = Format("hmset %s:%s %s %s %s %hu %s %lu", string(EASY_REDIS_EASYCMS), id, string(EASY_REDIS_IP), cmsIp,
				string(EASY_REDIS_PORT), cmsPort, string(EASY_REDIS_LOAD), load);
			auto replyHmset = static_cast<redisReply*>(redisCommand(redisContext_, hmset.c_str()));
			RedisReplyObjectDeleter replyHmsetDeleter(replyHmset);
			if (!replyHmset)
			{
				theRet = QTSS_NotConnected;
				break;
			}

			auto replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));
			RedisReplyObjectDeleter replyExpireDeleter(replyExpire);
			if (!replyExpire)
			{
				theRet = QTSS_NotConnected;
				break;
			}
		}
		else if (reply->integer == 1)
		{
			auto hset = Format("hset %s:%s %s %lu", server, id, string(EASY_REDIS_LOAD), load);
			auto replyHset = static_cast<redisReply*>(redisCommand(redisContext_, hset.c_str()));
			RedisReplyObjectDeleter replyHsetDeleter(replyHset);
			if (!replyHset)
			{
				theRet = QTSS_NotConnected;
				break;
			}
		}

	} while (0);

	if (theRet != QTSS_NoErr)
	{
		RedisErrorHandler();
	}

	return theRet;
}


QTSS_Error RedisSetDevice(Easy_DeviceInfo_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	if (!RedisConnect())
	{
		return QTSS_NotConnected;
	}

	if (!inParams->serial_ || string(inParams->serial_).empty())
	{
		return QTSS_BadArgument;
	}

	QTSS_Error theRet = QTSS_NoErr;

	do
	{
		string id(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
		auto hmset = Format("hmset %s:%s %s %s %s %s %s %s %s %s", string(EASY_REDIS_DEVICE), string(inParams->serial_),
			string(EASY_REDIS_DEVICE_TYPE), string(inParams->deviceType_), string(EASY_REDIS_TYPE), string(inParams->type_),
			string(EASY_REDIS_CHANNEL), string(inParams->channels_), string(EASY_REDIS_EASYCMS), id,
			string(EASY_REDIS_TOKEN), string(inParams->token_));
		auto reply = static_cast<redisReply*>(redisCommand(redisContext_, hmset.c_str()));
		RedisReplyObjectDeleter replyDeleter(reply);
		if (!reply)
		{
			theRet = QTSS_NotConnected;
			break;
		}

		if (string(reply->str) == string("OK"))
		{
			auto expire = Format("expire %s:%s 150", string(EASY_REDIS_DEVICE), string(inParams->serial_));
			auto replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));
			RedisReplyObjectDeleter replyExpireDeleter(replyExpire);
			if (!replyExpire)
			{
				theRet = QTSS_NotConnected;
				break;
			}
		}
		else
		{
			theRet = QTSS_RequestFailed;
		}

	} while (0);

	if (theRet != QTSS_NoErr)
	{
		RedisErrorHandler();
	}

	return theRet;
}

QTSS_Error RedisDelDevice(Easy_DeviceInfo_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	if (!RedisConnect())
	{
		return QTSS_NotConnected;
	}

	if (!inParams->serial_ || string(inParams->serial_).empty())
	{
		return QTSS_BadArgument;
	}

	QTSS_Error theRet = QTSS_NoErr;

	do
	{
		auto del = Format("del %s:%s", EASY_REDIS_DEVICE, string(inParams->serial_));
		auto reply = static_cast<redisReply*>(redisCommand(redisContext_, del.c_str()));
		RedisReplyObjectDeleter replyDeleter(reply);

		if (!reply)
		{
			theRet = QTSS_NotConnected;
			break;
		}

		if (reply->integer == 0)
		{
			theRet = QTSS_RequestFailed;
		}

	} while (0);

	if (theRet != QTSS_NoErr)
	{
		RedisErrorHandler();
	}

	return theRet;
}

QTSS_Error RedisGetAssociatedDarwin(QTSS_GetAssociatedDarwin_Params* inParams)
{
	OSMutexLocker mutexLock(&sMutex);

	if (!RedisConnect())
	{
		return QTSS_NotConnected;
	}

	QTSS_Error theRet = QTSS_NoErr;

	do
	{
		string exists = Format("exists %s:%s/%s", string(EASY_REDIS_LIVE), string(inParams->inSerial), string(inParams->inChannel));
		auto reply = static_cast<redisReply*>(redisCommand(redisContext_, exists.c_str()));
		RedisReplyObjectDeleter replyDeleter(reply);

		if (!reply)
		{
			theRet = QTSS_NotConnected;
			break;
		}

		if (reply->integer == 1)
		{
			string strTemp = Format("hmget %s:%s/%s %s", string(EASY_REDIS_LIVE), string(inParams->inSerial),
				string(inParams->inChannel), string(EASY_REDIS_EASYDARWIN));
			auto replyHmget = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
			RedisReplyObjectDeleter replyHmgetDeleter(replyHmget);
			if (!replyHmget)
			{
				theRet = QTSS_NotConnected;
				break;
			}
			string easydarwin = Format("%s:", string(EASY_REDIS_EASYDARWIN));
			easydarwin += replyHmget->element[0]->str;

			strTemp = Format("hmget %s %s %s %s", easydarwin, string(EASY_REDIS_IP), string(EASY_REDIS_HTTP),
				string(EASY_REDIS_RTSP));
			auto replyHmgetEasyDarwin = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
			RedisReplyObjectDeleter replyHmgetEasyDarwinDeleter(replyHmgetEasyDarwin);
			if (!replyHmgetEasyDarwin)
			{
				theRet = QTSS_NotConnected;
				break;
			}

			if (replyHmgetEasyDarwin->type == EASY_REDIS_REPLY_NIL)
			{
				theRet = QTSS_RequestFailed;
				break;;
			}

			if (replyHmgetEasyDarwin->type == EASY_REDIS_REPLY_ARRAY && replyHmgetEasyDarwin->elements == 3)
			{
				bool ok = true;
				for (int i = 0; i < replyHmgetEasyDarwin->elements; ++i)
				{
					if (replyHmgetEasyDarwin->element[i]->type == EASY_REDIS_REPLY_NIL)
					{
						ok = ok && false;
					}
				}

				if (ok)
				{
					string ip(replyHmgetEasyDarwin->element[0]->str);
					string httpPort(replyHmgetEasyDarwin->element[1]->str);
					string rtspPort(replyHmgetEasyDarwin->element[2]->str);
					memcpy(inParams->outDssIP, ip.c_str(), ip.size());
					memcpy(inParams->outHTTPPort, httpPort.c_str(), httpPort.size());
					memcpy(inParams->outDssPort, rtspPort.c_str(), rtspPort.size());
					inParams->isOn = true;
				}
				else
				{
					theRet = QTSS_RequestFailed;
					break;
				}
			}
		}
		else
		{
			string keys = Format("keys %s:*", string(EASY_REDIS_EASYDARWIN));
			auto replyKeys = static_cast<redisReply*>(redisCommand(redisContext_, keys.c_str()));
			RedisReplyObjectDeleter replyKeysDeleter(replyKeys);
			if (!replyKeys)
			{
				theRet = QTSS_NotConnected;
				break;
			}

			if (replyKeys->elements > 0)
			{
				int eleIndex = -1, eleLoad = 0;
				for (size_t i = 0; i < replyKeys->elements; ++i)
				{
					auto replyTemp = replyKeys->element[i];
					if (replyTemp->type == EASY_REDIS_REPLY_NIL)
					{
						continue;
					}

					string strTemp = Format("hmget %s %s %s %s %s ", string(replyTemp->str), string(EASY_REDIS_LOAD), string(EASY_REDIS_IP),
						string(EASY_REDIS_HTTP), string(EASY_REDIS_RTSP));
					auto replyHmget = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
					RedisReplyObjectDeleter replyHmgetDeleter(replyHmget);

					if (!replyHmget)
					{
						theRet = QTSS_NotConnected;
						break;
					}

					if (replyHmget->type == EASY_REDIS_REPLY_NIL)
					{
						continue;
					}

					auto loadReply = replyHmget->element[0];
					auto ipReply = replyHmget->element[1];
					auto httpReply = replyHmget->element[2];
					auto rtspReply = replyHmget->element[3];

					auto load = stoi(loadReply->str);
					string ip(ipReply->str);
					string http(httpReply->str);
					string rtsp(rtspReply->str);

					if (eleIndex == -1)
					{
						eleIndex = i;
						eleLoad = load;
						strncpy(inParams->outDssIP, ip.c_str(), ip.size());
						strncpy(inParams->outHTTPPort, http.c_str(), http.size());
						strncpy(inParams->outDssPort, rtsp.c_str(), rtsp.size());
					}
					else
					{
						if (load < eleLoad)//find better
						{
							eleIndex = i;
							eleLoad = load;
							strncpy(inParams->outDssIP, ip.c_str(), ip.size());
							strncpy(inParams->outHTTPPort, http.c_str(), http.size());
							strncpy(inParams->outDssPort, rtsp.c_str(), rtsp.size());
						}
					}
				}

				if (eleIndex == -1)//no one live
				{
					theRet = QTSS_Unimplemented;
					break;
				}
				else
				{
					inParams->isOn = false;
				}
			}
			else
			{
				theRet = QTSS_Unimplemented;
				break;
			}
		}

	} while (0);

	if (theRet != QTSS_NoErr)
	{
		RedisErrorHandler();
	}

	return theRet;
}

static void RedisErrorHandler()
{
	sIfConSucess = false;
	if (redisContext_)
	{
		printf("Connection error: %s\n", redisContext_->errstr);
		redisFree(redisContext_);
	}
	redisContext_ = NULL;
}