/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013 - 2020  LiteSpeed Technologies, Inc.                 *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include "httplog.h"

#include <http/accesslog.h>
#include <http/httpserverconfig.h>
#include <http/serverprocessconfig.h>
#include <http/stderrlogger.h>

#include <log4cxx/appender.h>
#include <log4cxx/layout.h>
#include <log4cxx/logger.h>
#include <log4cxx/logrotate.h>
#include <quic/quicengine.h>

#include <util/gpath.h>
#include <new>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

using namespace LOG4CXX_NS;
#define ACCESS_INIT 1
static char                 achAccessLog[ sizeof(AccessLog) ];
static Logger              *s_pLogger = NULL;
static char                 s_logPattern[40] = "%d [%p] %m";

static const char          *s_pLogId = NULL;
static LOG4CXX_NS::Logger  *s_pCurLogger  = NULL;

int HttpLog::s_debugLevel = DL_IODATA;
AutoStr2 HttpLog::s_serverRoot = "";

void HttpLog::parse_error(const char *pCurLine, const char *pError)
{
    LS_ERROR(s_pCurLogger, "[%s] rewrite: %s while parsing: %s",
             s_pLogId, pError, pCurLine);
}


void HttpLog::parse_warn(const char *pCurLine, const char *pError)
{
    LS_WARN(s_pCurLogger, "[%s] rewrite: %s while parsing: %s",
             s_pLogId, pError, pCurLine);
}


void HttpLog::setCurLogger(LOG4CXX_NS::Logger *pLogger, const char *pId)
{
    s_pCurLogger = pLogger;
    s_pLogId = pId;
}


void HttpLog::perror(const char *pStr, const char *pError)
{
    LS_ERROR(s_pCurLogger, "[%s] %s: %s.",
             s_pLogId, (pStr) ? pStr : "", pError);
}


static inline AccessLog *accessLog()
{   return (AccessLog *)achAccessLog;     }


static inline Logger *logger()
{   return s_pLogger;       }


HttpLog::HttpLog()
{
}


HttpLog::~HttpLog()
{
}


void HttpLog::init()
{

    s_pLogger = Logger::getRootLogger() ;
    new(achAccessLog) AccessLog("");
    Appender *appender
        = Appender::getAppender("stderr", "appender.ps");
    Layout *layout = Layout::getLayout(ERROR_LOG_PATTERN, "layout.pattern");
    layout->setUData(s_logPattern);
    appender->setLayout(layout);
    logger()->setLevel(Level::DEBUG);
    logger()->setAppender(appender);
}


void HttpLog::updateLogPatternWithPid(int pid)
{
    lsnprintf( s_logPattern, sizeof(s_logPattern), "%%d [%%p] [%d] %%m", pid);
}


bool HttpLog::isEnabled(Logger *pLogger, int level)
{
    if (pLogger)
        return pLogger->isEnabled(level);
    else
        return logger()->isEnabled(level);
}


bool HttpLog::isDebugEnabled(Logger *pLogger, int level)
{
    return (level < s_debugLevel) && (isEnabled(pLogger, Level::DEBUG));
}


void HttpLog::setDebugLevel(int level)
{
    s_debugLevel = level;
    if (level == 0)
    {
        if (log4cxx::Level::isEnabled(Level::DEBUG))
            log4cxx::Level::setDefaultLevel(Level::INFO);
        QuicEngine::setDebugLog(0);
    }
    else
    {
        logger()->setLevel(Level::DEBUG);
        log4cxx::Level::setDefaultLevel(Level::DEBUG + 10 * level);
        QuicEngine::setDebugLog(1);
    }
}

void HttpLog::toggleDebugLog()
{
    if (s_debugLevel)
        setDebugLevel(0);
    else
        setDebugLevel(10);
}


int  HttpLog::getLogLevel()
{
    return logger()->getLevel();
}


void HttpLog::setLogLevel(int level)
{
    logger()->setLevel(level);
}


void HttpLog::setLogLevel(const char *pLevel)
{
    logger()->setLevel(pLevel);
}


void HttpLog::setLogPattern(const char *pPattern)
{
    if (strlen(pPattern) < sizeof(s_logPattern) - 1)
        lstrncpy(s_logPattern, pPattern, sizeof(s_logPattern));
}


const char *HttpLog::getLogPattern()
{
    return s_logPattern;
}


LOG4CXX_NS::Logger *HttpLog::getErrorLogger()
{
    return logger();
}

#define MAX_PATH_LEN                8192
int HttpLog::logAccess(const char *pVHost, int len, HttpSession *pSession)
{
    accessLog()->log(pVHost, len, pSession);
    return 0;
}


int HttpLog::checkLogPathValid(const char *org)
{
    const char *excludeFileList[] = { ".cgi", ".pl", ".shtml" };
    const char *excludeDirList[] = { "admin/conf", "admin/html", "conf" };
    char real[MAX_PATH_LEN+1];
    const char *pFileName = real;
    char *end = lstrncpy(real, org, MAX_PATH_LEN);
    int len = end - real;
    int ret = GPath::checkSymLinks(real, end, real + MAX_PATH_LEN, real, 1);
    if (ret == -1)
        pFileName = org;
    else
        len = ret;
    if (len < 5)
        return 0;

    unsigned i;
    for (i=0; i<sizeof(excludeFileList)/ sizeof(char *); ++i)
    {
        int ll = strlen(excludeFileList[i]);
        if (len > ll &&
            strncasecmp(pFileName + len - ll, excludeFileList[i], ll) == 0)
        {
            HttpLog::perror("Cannot use this suffix as log file", pFileName);
            return LS_FAIL;
        }
    }

    //For special ".ph???"
    const char *pExt = strrchr(pFileName, '.');
    if (pExt && strlen(pExt) >= 3 && strncasecmp(pExt, ".ph", 3) == 0)
    {
        HttpLog::perror("Cannot use the suffix as log file", pFileName);
        return LS_FAIL;
    }


    if (strncmp(pFileName, "/etc/", 5) == 0)
    {
        if (strncasecmp(pFileName + 5, "apache", 6) == 0
            || strncasecmp(pFileName + 5, "httpd", 5) == 0)
            return LS_FAIL;
        return 0;
    }

    if (len > s_serverRoot.len() + 4 &&
        strncmp(pFileName, s_serverRoot.c_str(), s_serverRoot.len()) == 0)
    {
        for (i=0; i<sizeof(excludeDirList)/ sizeof(char *); ++i)
        {
            int ll = strlen(excludeDirList[i]);
            if (len - s_serverRoot.len() > ll &&
                strncasecmp(pFileName + s_serverRoot.len(),
                            excludeDirList[i], ll) == 0)
            {
                HttpLog::perror("Cannot use this directory for log file", pFileName);
                return LS_FAIL;
            }
        }
    }

    return 0;
}


int HttpLog::setAccessLogFile(const char *pFileName, int pipe)
{
    int ret = checkLogPathValid(pFileName);
    if (ret)
        return ret;
    ret = accessLog()->init(pFileName, pipe);
    return ret;
}


AccessLog *HttpLog::getAccessLog()
{
    return accessLog();
}


int HttpLog::setErrorLogFile(const char *pFileName)
{
    int ret = checkLogPathValid(pFileName);
    if (ret)
        return ret;

    Appender *appender
        = Appender::getAppender(pFileName, "appender.ps");
    if (appender)
    {
        Appender *pOld = logger()->getAppender();
        if ((pOld) && (pOld != appender))
            pOld->close();
        Layout *layout = Layout::getLayout(ERROR_LOG_PATTERN, "layout.pattern");
        appender->setLayout(layout);
        logger()->setAppender(appender);
        return 0;
    }
    else
        return LS_FAIL;
}


const char *HttpLog::getAccessLogFileName()
{
    return accessLog()->getLogPath();
}


const char *HttpLog::getErrorLogFileName()
{
    return logger()->getAppender()->getName();
}


void HttpLog::offsetChroot(const char *pRoot, int len)
{
    char achTemp[2048];
    if (!accessLog()->isPipedLog() &&
        (strncmp(pRoot, getAccessLogFileName(), len) == 0))
    {
        memccpy(achTemp, getAccessLogFileName() + len, 0, sizeof(achTemp) - 1);
        accessLog()->getAppender()->setName(achTemp);
    }
    if (strncmp(pRoot, getErrorLogFileName(), len) == 0)
    {
        logger()->getAppender()->close();
        off_t rollSize = logger()->getAppender()->getRollingSize();
        memccpy(achTemp, getErrorLogFileName() + len, 0, sizeof(achTemp) - 1);
        setErrorLogFile(achTemp);
        logger()->getAppender()->setRollingSize(rollSize);
    }

}


void HttpLog::error_num(int __errnum, const char *__file,
                        unsigned int __line, const char *__function)
{
    LS_ERROR(logger(),
             "errno: (%d)%s in file:%s line:%d function:%s\n",
             __errnum, strerror(__errnum),
             __file, __line, __function);
}


void HttpLog::error_detail(const char *__errstr, const char *__file,
                           unsigned int __line, const char *__function)
{
    LS_ERROR(logger(), "error:%s in file:%s line:%d function:%s\n",
             __errstr, __file, __line, __function);
}


void HttpLog::error(const char *fmt, ...)
{
    if (logger()->isEnabled(LOG4CXX_NS::Level::ERROR))
    {
        va_list ap;
        va_start(ap, fmt);
        logger()->verror(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::warn(const char *fmt, ...)
{
    if (logger()->isEnabled(LOG4CXX_NS::Level::WARN))
    {
        va_list ap;
        va_start(ap, fmt);
        logger()->vwarn(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::debug(const char *fmt, ...)
{
    if (s_debugLevel > 0)
    {
        if (logger()->isEnabled(LOG4CXX_NS::Level::DEBUG))
        {
            va_list ap;
            va_start(ap, fmt);
            logger()->vdebug(fmt, ap);
            va_end(ap);
        }
    }
}


void HttpLog::notice(const char *fmt, ...)
{
    if (logger()->isEnabled(LOG4CXX_NS::Level::NOTICE))
    {
        va_list ap;
        va_start(ap, fmt);
        logger()->vnotice(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::info(const char *fmt, ...)
{
    if (logger()->isEnabled(LOG4CXX_NS::Level::INFO))
    {
        va_list ap;
        va_start(ap, fmt);
        logger()->vinfo(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::vlog(int level, const char *format, va_list args)
{
    logger()->vlog(level,  format, args);
}


void HttpLog::log(int level, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logger()->vlog(level,  fmt, ap);
    va_end(ap);

}


void HttpLog::vlog(LOG4CXX_NS::Logger *pLogger, int level,
                   const char *format, va_list args, int no_linefeed)
{
    if (!pLogger)
        pLogger = logger();
    if (pLogger->isEnabled(level))
        pLogger->vlog(level, format, args, no_linefeed);
}


void HttpLog::lograw(LOG4CXX_NS::Logger *pLogger, const char *pBuf,
                     int len)
{
    if (!pLogger)
        pLogger = logger();
    pLogger->lograw(pBuf, len);
}


void HttpLog::error(Logger *pLogger, const char *fmt, ...)
{
    if (!pLogger)
        pLogger = logger();
    if (pLogger->isEnabled(LOG4CXX_NS::Level::ERROR))
    {
        va_list ap;
        va_start(ap, fmt);
        pLogger->verror(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::warn(Logger *pLogger, const char *fmt, ...)
{
    if (!pLogger)
        pLogger = logger();
    if (pLogger->isEnabled(LOG4CXX_NS::Level::WARN))
    {
        va_list ap;
        va_start(ap, fmt);
        pLogger->vwarn(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::debug(Logger *pLogger, const char *fmt, ...)
{
    if (s_debugLevel > 0)
    {
        if (!pLogger)
            pLogger = logger();
        if (pLogger->isEnabled(LOG4CXX_NS::Level::DEBUG))
        {
            va_list ap;
            va_start(ap, fmt);
            pLogger->vdebug(fmt, ap);
            va_end(ap);
        }
    }
}


void HttpLog::notice(Logger *pLogger, const char *fmt, ...)
{
    if (!pLogger)
        pLogger = logger();
    if (pLogger->isEnabled(LOG4CXX_NS::Level::NOTICE))
    {
        va_list ap;
        va_start(ap, fmt);
        pLogger->vnotice(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::info(Logger *pLogger, const char *fmt, ...)
{
    if (!pLogger)
        pLogger = logger();
    if (pLogger->isEnabled(LOG4CXX_NS::Level::INFO))
    {
        va_list ap;
        va_start(ap, fmt);
        pLogger->vinfo(fmt, ap);
        va_end(ap);
    }
}


void HttpLog::errmem(const char *pSource)
{
    LS_ERROR("Out of memory: %s", pSource);
}


void syntax_check()
{
    LS_DBG_L("This is a test, %s %d \n", "string", 23423);
    LS_ERROR("This is a test, %s %d \n", "string", 23423);
    LOG_ERR_CODE(1);
    LOG_DERR("errstr");
}


void HttpLog::onTimer()
{
    if (!logger() || !logger()->getAppender())
        return;

    ServerProcessConfig &procConfig = ServerProcessConfig::getInstance();
    if (HttpServerConfig::getInstance().getProcNo())
    {
        accessLog()->reopenExist();
        accessLog()->flush();
        logger()->getAppender()->reopenExist();
        logger()->getAppender()->flush();
        StdErrLogger::getInstance().getAppender()->reopenExist();
    }
    else
    {
        if (!accessLog()->isPipedLog())
            LogRotate::testAndRoll(accessLog()->getAppender(),
                                   procConfig.getUid(), procConfig.getGid());
        LogRotate::testAndRoll(logger()->getAppender(),
                               procConfig.getUid(), procConfig.getGid());
        LogRotate::testAndRoll(StdErrLogger::getInstance().getAppender(),
                               procConfig.getUid(), procConfig.getGid());

    }
}





