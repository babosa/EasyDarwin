#ifndef __EASY_REDIS_HANDLER_H__
#define __EASY_REDIS_HANDLER_H__

#include "OSHeaders.h"
#include "QTSServerInterface.h"
#ifdef WIN32
#include "Windows/hiredis.h"
#else
#include "hiredis.h"
#endif //WIN32

#include "Task.h"

class RedisReplyObjectDeleter
{
public:
	RedisReplyObjectDeleter()
		: fReply(NULL)
	{}

    explicit RedisReplyObjectDeleter(redisReply* reply)
		: fReply(reply)
	{}

    ~RedisReplyObjectDeleter() 
	{ 
		if (fReply)
			freeReplyObject(fReply);
	}
        
    void ClearObject() { fReply = NULL; }

    void SetObject(redisReply* reply) 
    {
        fReply = reply; 
    }

    redisReply* GetObject() const { return fReply; }
    
private:    
    redisReply* fReply;

};

class EasyRedisHandler : public Task
{
public:
	EasyRedisHandler(const char* ip, UInt16 port, const char* passwd);
	virtual ~EasyRedisHandler();

	QTSS_Error RedisTTL();

    QTSS_Error RedisUpdateStream(Easy_StreamInfo_Params* inParams);
    QTSS_Error RedisSetRTSPLoad();
    QTSS_Error RedisGetAssociatedCMS(QTSS_GetAssociatedCMS_Params* inParams);
    QTSS_Error RedisJudgeStreamID(QTSS_JudgeStreamID_Params* inParams);
	
	OSQueueElem		fQueueElem;	
	UInt32			fID;
private:
	virtual SInt64	Run();
	bool RedisConnect();
	void RedisErrorHandler();

	bool			sIfConSucess;
	OSMutex			sMutex;
	redisContext*	redisContext_;

	char			fRedisIP[128];
	UInt16			fRedisPort;
	char			fRedisPasswd[256];
	
};



#endif //__EASY_REDIS_HANDLER_H__

