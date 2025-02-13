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
#ifndef CONTEXTTREE_H
#define CONTEXTTREE_H

class AutoStr2;
class HttpContext;
class ContextList;
class ContextNode;
class HttpVHost;
class StringList;
class HttpHandler;

class ContextTree
{
    ContextNode          *m_pRootNode;
    const HttpContext    *m_pRootContext;
    ContextNode          *m_pLocRootNode;
    char                 *m_pLocRootPath;
    int                   m_iLocRootPathLen;

    ContextTree(const ContextTree &rhs);
    void operator=(const ContextTree &rhs);

public:
    ContextTree();
    //ContextTree( const char * pPrefix, int len );
    ~ContextTree();

    const HttpContext *getRootContext() const  {   return m_pRootContext;  }

    const char   *getPrefix(int &iPrefixLen);
    ContextNode *getRootNode() const           {   return m_pRootNode;     }

    ContextNode *findNode(const char *pPrefix);

    ContextNode *addNode(const char *pPrefix, int len,
                         ContextNode *pCurNode,
                         char *pStart, long lastCheck = 0);

    HttpContext *removeMatchContext(const char *pURI);

    int add(HttpContext *pContext);
    const HttpContext *bestMatch(const char *pURI, int len) const;
    const HttpContext *matchLocation(const char *pLocation, int len) const;
    HttpContext *getContext(const char *pURI) const;
    HttpContext *getContext(const char *pURI, int regex) const;
    HttpContext *addContextByUri(const char *pBegin, int tmLastCheck);

    void setRootContext(const HttpContext *pContext)
    {   m_pRootContext = pContext;  }
    void setRootLocation(const char *pLocation);
    void contextInherit();

    const HttpContext *matchIndexes(const StringList *pIndexList,
                                    AutoStr2 *&pIdx) const;
    const HttpContext *matchIndex(const char *pIndex) const;

    //void merge( const ContextTree & rhs );
};


#endif
