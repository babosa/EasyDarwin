#include "EasyRedisHandler.h"

#include "QTSSMemoryDeleter.h"
#include "Format.h"
#include "Resources.h"
#include "EasyUtil.h"

static UInt32 sRedisHandlerID = 0;

EasyRedisHandler::EasyRedisHandler(const char* ip, UInt16 port, const char* passwd)
	: fQueueElem()
	, fID(sRedisHandlerID++)
	, sIfConSucess(false)
	, sMutex()
	, redisContext_(NULL)
{
	this->SetTaskName("EasyRedisHandler");

	fQueueElem.SetEnclosingObject(this);

	::strcpy(fRedisIP, ip);
	fRedisPort = port;
	::strcpy(fRedisPasswd, passwd);

	this->Signal(Task::kStartEvent);
}

EasyRedisHandler::~EasyRedisHandler()
{
	RedisErrorHandler();
}

SInt64 EasyRedisHandler::Run()
{
	OSMutexLocker locker(&sMutex);

	EventFlags theEvents = this->GetEvents();

	RedisConnect();

	return 0;
}

bool EasyRedisHandler::RedisConnect()
{
	if (sIfConSucess)
		return true;

	bool theRet = false;
	do
	{
		struct timeval timeout = { 2, 0 }; // 2 seconds
		redisContext_ = redisConnectWithTimeout(fRedisIP, fRedisPort, timeout);
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

		string auth = Format("auth %s", string(fRedisPasswd));
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

	} while (false);

	if (!theRet && redisContext_)
		RedisErrorHandler();

	return theRet;
}

QTSS_Error EasyRedisHandler::RedisTTL()
{
    //OSMutexLocker mutexLock(&sMutex);

    //if (!RedisConnect())
    //{
    //    return QTSS_NotConnected;
    //}
    //QTSS_Error theRet = QTSS_NoErr;

    //do
    //{
    //    string server(QTSServerInterface::GetServerName().Ptr);
    //    string guid(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
    //    UInt32 load = QTSServerInterface::GetServer()->GetNumRTPSessions();

    //    char chKey[512] = { 0 };
    //    sprintf(chKey, "expire %s:%s 15", server, guid);
    //    auto reply = static_cast<redisReply*>(redisCommand(redisContext_, chKey));

    //    RedisReplyObjectDeleter replyDeleter(reply);

    //    if (!reply)
    //    {
    //        printf("Redis expire EasyDarwin error\n");
    //        theRet = QTSS_NotConnected;
    //        break;
    //    }

    //    if (reply->integer == 0)
    //    {
    //        auto ip = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP();
    //        auto http = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWanPort();
    //        auto rtsp = QTSServerInterface::GetServer()->GetPrefs()->GetRTSPWANPort();

    //        sprintf(chKey, "hmset %s:%s %s %s %s %d %s %d %s %d", server, guid, EASY_REDIS_IP, ip, EASY_REDIS_HTTP,
    //            http, EASY_REDIS_RTSP, rtsp, EASY_REDIS_LOAD, load);
    //        auto replyHmset = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
    //        RedisReplyObjectDeleter replyHmsetDeleter(replyHmset);

    //        if (!replyHmset)
    //        {
    //            printf("Redis hmset EasyDarwin error\n");
    //            theRet = QTSS_NotConnected;
    //            break;
    //        }

    //        sprintf(chKey, "expire %s:%s 15", server, guid);
    //        auto replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, chKey));
    //        RedisReplyObjectDeleter replyExpireDeleter(replyExpire);

    //        if (!replyExpire)
    //        {
    //            printf("Redis expire new EasyDarwin error\n");
    //            theRet = QTSS_NotConnected;
    //            break;
    //        }
    //    }
    //    else if (reply->integer == 1)
    //    {
    //        //TODO::nothing
    //    }

    //} while (false);

    //if (theRet != QTSS_NoErr)
    //{
    //    RedisErrorHandler();
    //}

    //return QTSS_NoErr;

	OSMutexLocker mutexLock(&sMutex);

	QTSS_Error theRet = QTSS_NoErr;
	if (!RedisConnect())
		return QTSS_NotConnected;

	string server(QTSServerInterface::GetServer()->GetServerName().Ptr);
	string id(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
	UInt32 load = QTSServerInterface::GetServer()->GetNumRTPSessions();

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
			QTSSCharArrayDeleter wanIP(QTSServerInterface::GetServer()->GetPrefs()->GetServiceWANIP());
			UInt16 http = QTSServerInterface::GetServer()->GetPrefs()->GetServiceWanPort();
            UInt16 rtsp = QTSServerInterface::GetServer()->GetPrefs()->GetRTSPWANPort();
			std::string hmset = Format("hmset %s:%s %s %s %s %hu %s %hu %s %lu", string(EASY_REDIS_EASYDARWIN), id, string(EASY_REDIS_IP), string(wanIP),
				string(EASY_REDIS_HTTP), http, string(EASY_REDIS_RTSP), rtsp, string(EASY_REDIS_LOAD), load);
			redisReply* replyHmset = static_cast<redisReply*>(redisCommand(redisContext_, hmset.c_str()));
			RedisReplyObjectDeleter replyHmsetDeleter(replyHmset);
			if (!replyHmset)
			{
				theRet = QTSS_NotConnected;
				break;
			}

			redisReply* replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));
			RedisReplyObjectDeleter replyExpireDeleter(replyExpire);
			if (!replyExpire)
			{
				theRet = QTSS_NotConnected;
				break;
			}
		}
		else if (reply->integer == 1)
		{
			string hset = Format("hset %s:%s %s %lu", server, id, string(EASY_REDIS_LOAD), load);
			redisReply* replyHset = static_cast<redisReply*>(redisCommand(redisContext_, hset.c_str()));
			RedisReplyObjectDeleter replyHsetDeleter(replyHset);
			if (!replyHset)
			{
				theRet = QTSS_NotConnected;
				break;
			}
		}

	} while (false);

	if (theRet != QTSS_NoErr)
		RedisErrorHandler();

	return theRet;
}

QTSS_Error EasyRedisHandler::RedisUpdateStream(Easy_StreamInfo_Params* inParams)
{
    OSMutexLocker mutexLock(&sMutex);

    if (!RedisConnect())
    {
        return QTSS_NotConnected;
    }
    QTSS_Error theRet = QTSS_NoErr;

    do
    {
        if (inParams->inAction == easyRedisActionDelete)
        {
            string del = Format("del %s:%s/%lu", string(EASY_REDIS_LIVE), string(inParams->inStreamName), inParams->inChannel);
            redisReply* replyDel = static_cast<redisReply*>(redisCommand(redisContext_, del.c_str()));
            RedisReplyObjectDeleter replyDeleter(replyDel);

            if (!replyDel)
            {
                printf("Redis del Live error\n");
                theRet = QTSS_NotConnected;
                break;
            }
            return QTSS_NoErr;
        }
        string id(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
        string hmset = Format("hmset %s:%s/%lu %s %lu %s %lu %s %s", string(EASY_REDIS_LIVE), string(inParams->inStreamName), inParams->inChannel,
            string(EASY_REDIS_BITRATE), inParams->inBitrate, string(EASY_REDIS_OUTPUT), inParams->inNumOutputs,
            string(EASY_REDIS_EASYDARWIN), id);
        redisReply* replyHmset = static_cast<redisReply*>(redisCommand(redisContext_, hmset.c_str()));
        RedisReplyObjectDeleter replyDeleter(replyHmset);

        if (!replyHmset)
        {
            printf("Redis hmset Live error\n");
            theRet = QTSS_NotConnected;
            break;
        }

        if (replyHmset->str && (replyHmset->str) == string("OK"))
        {
            string expire = Format("expire %s:%s/%lu 150", string(EASY_REDIS_LIVE), string(inParams->inStreamName), inParams->inChannel);
            redisReply* replyExpire = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));
            RedisReplyObjectDeleter replyExpireDeleter(replyExpire);

            if (!replyExpire)
            {
                printf("Redis expire Live error\n");

                theRet = QTSS_NotConnected;
                break;
            }
        }
        else
        {
            theRet = QTSS_NotConnected;
            break;
        }

    } while (false);

    if (theRet != QTSS_NoErr)
    {
        RedisErrorHandler();
    }

    return QTSS_NoErr;
}

QTSS_Error EasyRedisHandler::RedisSetRTSPLoad()
{
    OSMutexLocker mutexLock(&sMutex);

    if (!RedisConnect())
    {
        return QTSS_NotConnected;
    }
    QTSS_Error theRet = QTSS_NoErr;

    do
    {
        string server(QTSServerInterface::GetServer()->GetServerName().Ptr);
        string id(QTSServerInterface::GetServer()->GetCloudServiceNodeID());
        UInt32 load = QTSServerInterface::GetServer()->GetNumRTPSessions();

        string expire = Format("expire %s:%s 15", server, id);
        redisReply* reply = static_cast<redisReply*>(redisCommand(redisContext_, expire.c_str()));
        RedisReplyObjectDeleter replyDeleter(reply);

        if (!reply)
        {
            printf("Redis expire EasyDarwin error\n");
            theRet = QTSS_NotConnected;
            break;
        }

        if (reply->integer == 1)
        {
            string hset = Format("hset %s:%s %s %lu", server, id, string(EASY_REDIS_LOAD), load);
            redisReply* replyHset = static_cast<redisReply*>(redisCommand(redisContext_, hset.c_str()));
            RedisReplyObjectDeleter replyHsetDeleter(replyHset);

            if (!replyHset)
            {
                printf("Redis hset EasyDarwin Load error\n");
                theRet = QTSS_NotConnected;
                break;
            }
        }
    } while (false);

    if (theRet != QTSS_NoErr)
    {
        RedisErrorHandler();
    }
    return theRet;
}

QTSS_Error EasyRedisHandler::RedisGetAssociatedCMS(QTSS_GetAssociatedCMS_Params* inParams)
{
    OSMutexLocker mutexLock(&sMutex);

    if (!RedisConnect())
    {
        return QTSS_NotConnected;
    }

    QTSS_Error theRet = QTSS_NoErr;

    do
    {
        string exists = Format("exists %s:%s", string(EASY_REDIS_DEVICE), string(inParams->inSerial));
        redisReply* reply = static_cast<redisReply*>(redisCommand(redisContext_, exists.c_str()));
        RedisReplyObjectDeleter replyDeleter(reply);

        if (!reply)
        {
            printf("Redis exists Device error\n");
            theRet = QTSS_NotConnected;
            break;
        }

        if (reply->integer == 1)
        {
            string strTemp = Format("hmget %s:%s %s", string(EASY_REDIS_DEVICE), string(inParams->inSerial),
                string(EASY_REDIS_EASYCMS));
            redisReply* replyHmget = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
            RedisReplyObjectDeleter replyHmgetDeleter(replyHmget);

            if (!replyHmget)
            {
                printf("Redis hmget Device error\n");
                theRet = QTSS_NotConnected;
                break;
            }

            string easycms = Format("%s:", string(EASY_REDIS_EASYCMS));
            easycms += replyHmget->element[0]->str;

            strTemp = Format("hmget %s %s %s ", easycms, string(EASY_REDIS_IP), string(EASY_REDIS_PORT));
            redisReply* replyHmgetEasyDarwin = static_cast<redisReply*>(redisCommand(redisContext_, strTemp.c_str()));
            RedisReplyObjectDeleter replyHmgetEasyDarwinDeleter(replyHmgetEasyDarwin);

            if (!replyHmgetEasyDarwin)
            {
                printf("Redis hmget EasyCMS error\n");

                theRet = QTSS_NotConnected;
                break;
            }

            if (replyHmgetEasyDarwin->type == REDIS_REPLY_NIL)
            {
                theRet = QTSS_RequestFailed;
                break;
            }

            if (replyHmgetEasyDarwin->type == REDIS_REPLY_ARRAY && replyHmgetEasyDarwin->elements == 2)
            {
                bool ok = true;
                for (int i = 0; i < replyHmgetEasyDarwin->elements; ++i)
                {
                    if (replyHmgetEasyDarwin->element[i]->type == REDIS_REPLY_NIL)
                    {
                        ok = ok && false;
                    }
                }

                if (ok)
                {
                    memcpy(inParams->outCMSIP, replyHmgetEasyDarwin->element[0]->str, replyHmgetEasyDarwin->element[0]->len);
                    memcpy(inParams->outCMSPort, replyHmgetEasyDarwin->element[1]->str, replyHmgetEasyDarwin->element[1]->len);
                }
                else
                {
                    theRet = QTSS_RequestFailed;
                    break;
                }
            }
        }

    } while (false);

    if (theRet != QTSS_NoErr)
    {
        RedisErrorHandler();
    }
    return theRet;
}

QTSS_Error EasyRedisHandler::RedisJudgeStreamID(QTSS_JudgeStreamID_Params* inParams)
{
    ////算法描述，删除指定sessionID对应的key，如果成功删除，表明SessionID存在，验证通过，否则验证失败
    //OSMutexLocker mutexLock(&sMutex);
    //if (!sIfConSucess)
    //	return QTSS_NotConnected;

    //char chKey[128] = { 0 };
    //sprintf(chKey, "SessionID_%s", inParams->inStreanID);//如果key存在则返回整数类型1，否则返回整数类型0

    //int ret = sRedisClient->Delete(chKey);

    //if (ret == -1)//fatal err,need reconnect
    //{
    //	sRedisClient->Free();
    //	sIfConSucess = false;

    //	return QTSS_NotConnected;
    //}
    //else if (ret == 0)
    //{
    //	*(inParams->outresult) == 1;
    //	return QTSS_NoErr;
    //}
    //else
    //{
    //	return ret;
    //}
    return 0;
}

void EasyRedisHandler::RedisErrorHandler()
{
	sIfConSucess = false;
	if (redisContext_)
	{
		printf("Connection error: %s\n", redisContext_->errstr);
		redisFree(redisContext_);
	}
	redisContext_ = NULL;
}
