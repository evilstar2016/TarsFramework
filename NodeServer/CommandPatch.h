﻿/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except 
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed 
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 * specific language governing permissions and limitations under the License.
 */

#ifndef __PATCH_COMMAND_H_
#define __PATCH_COMMAND_H_

#include "SingleFileDownloader.h"
#include "tars_patch.h"
#include "NodeDescriptor.h"
#include "RegistryProxy.h"
#include "util/tc_file.h"
#include "util/tc_md5.h"
#include "ServerCommand.h"
#include "NodeRollLogger.h"
#include "CommandStop.h"
#include "util.h"

class CommandPatch : public ServerCommand
{
public:
    CommandPatch(const ServerObjectPtr & server, const std::string & sDownloadPath, const tars::PatchRequest & request);

    ExeStatus canExecute(std::string &sResult);

    int execute(std::string &sResult);

    int updatePatchResult(std::string &sResult);

    int download(const std::string & sRemoteTgzPath, const std::string & sLocalTgzPath, const std::string & sShortFileName, const std::string& reqMd5, std::string & sResult);

    static string getOsType();

    //是否检查操作系统类型
    static bool checkOsType();

private:
    int backupfiles(std::string &sResult);

private:
    ServerObjectPtr     _serverObjectPtr;

    tars::PatchRequest   _patchRequest;

    //本地存放tgz的目录
    std::string         _localTgzBasePath;

    //本地解压的文件目录
    std::string         _localExtractBasePath;

    StatExChangePtr     _statExChange;
};

class PatchDownloadEvent : public DownloadEvent
{
public:
    PatchDownloadEvent(ServerObjectPtr pServerObjectPtr)
    : _serverObjectPtr(pServerObjectPtr)
    {}

    virtual void onDownloading(const FileInfo &fi, int pos)
    {
        if (_serverObjectPtr && fi.size > 0)
        {
            _serverObjectPtr->setPatchPercent((int)(pos*100.0/fi.size));
        }
    }

private:
    ServerObjectPtr     _serverObjectPtr;
};

//////////////////////////////////////////////////////////////
//
inline CommandPatch::CommandPatch(const ServerObjectPtr & server, const std::string & sDownloadPath, const tars::PatchRequest & request)
: _serverObjectPtr(server), _patchRequest(request)
{
    _localTgzBasePath      = sDownloadPath + FILE_SEP + "BatchPatchingLoad";
    _localExtractBasePath  = sDownloadPath + FILE_SEP + "BatchPatching";
}

inline string CommandPatch::getOsType()
{
    string defaultOs = g_pconf->get("/tars/node<default_os>", "suse");
    string ret = defaultOs;

    vector<string> vKey = g_pconf->getDomainKey("/tars/node/os_pattern");
    NODE_LOG("patchPro")->debug() << FILE_FUN<< vKey.size() << endl;
    for(size_t i = 0; i < vKey.size(); i++)
    {
        string key   = vKey[i];
        string paths = g_pconf->get("/tars/node/os_pattern<" + key + ">", "");
        NODE_LOG("patchPro")->debug() <<FILE_FUN<< paths << endl;
        vector<string> vPath = TC_Common::sepstr<string>(paths,":");
        vector<string>::iterator it = vPath.begin();
        for(; it != vPath.end(); it ++)
        {
            string path = *it;
            if(TC_File::isFileExist(path))
            {
                ret = key;
                NODE_LOG("patchPro")->debug() << FILE_FUN<< path << " is exist, ret:" << ret << endl;
                return ret;
            }
            else
            {
                NODE_LOG("patchPro")->debug() << FILE_FUN<< path << " not exist" << endl;
            }
        }
    }

    return ret;
}

inline bool CommandPatch::checkOsType()
{
    string checkStr = g_pconf->get("/tars/node<enable_os_pattern>", "n");
    if(checkStr == "Y" || checkStr == "y" || checkStr == "yes" || checkStr == "true")
    {
        return true;
    }
    else
    {
        return false;
    }
}


inline ServerCommand::ExeStatus CommandPatch::canExecute(string & sResult)
{
    ServerObject::InternalServerState eState        = _serverObjectPtr->getLastState();

    ServerObject::InternalServerState eCurrentState =_serverObjectPtr->getInternalState();

    NODE_LOG(_serverObjectPtr->getServerId())->debug()<< FILE_FUN <<_patchRequest.appname+"."+_patchRequest.servername<<"|sResult:"<<sResult
        <<"|current state"<<_serverObjectPtr->toStringState(eCurrentState)<<"|last state :"<< _serverObjectPtr->toStringState(eState)<< endl;

    _statExChange  = new StatExChange(_serverObjectPtr, ServerObject::BatchPatching, eState);

    _serverObjectPtr->setPatchVersion(_patchRequest.version);
    _serverObjectPtr->setPatchPercent(0);
    _serverObjectPtr->setPatchResult("");

    return EXECUTABLE;
}

inline int CommandPatch::updatePatchResult(string & sResult)
{
    try
    {
        NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << endl;

        //服务发布成功，向主控发送UPDDATE命令
        RegistryPrx proxy = AdminProxy::getInstance()->getRegistryProxy();
        if (!proxy)
        {
            sResult = "patch succ but update version and user fault, get registry proxy fail";

            NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|" << sResult << endl;

            g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 

            // g_app.reportServer(_patchRequest.appname + "." + _patchRequest.servername, sResult);

            return -1;
        }

        //向主控发送命令，设置该服务的版本号和发布者
        struct PatchResult patch;
        patch.sApplication  = _patchRequest.appname;
        patch.sServerName   = _patchRequest.servername;
        patch.sNodeName     = _patchRequest.nodename;
        patch.sVersion      = _patchRequest.version;
        patch.sUserName     = _patchRequest.user;

        int iRet = proxy->updatePatchResult(patch);
        NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Update:" << iRet << endl;
    }
    catch (exception& e)
    {
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Exception:" << e.what() << endl;
        sResult = _patchRequest.appname + "." + _patchRequest.servername + "|Exception:" + e.what();
        g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 
        // g_app.reportServer(_patchRequest.appname + "." + _patchRequest.servername, sResult);
        return -1;
    }
    catch (...)
    {
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Unknown Exception" << endl;
        sResult = _patchRequest.appname + "." + _patchRequest.servername + "|Unknown Exception";
        g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 
        // g_app.reportServer(_patchRequest.appname + "." + _patchRequest.servername, sResult);
        return -1;
    }

    return 0;
}


inline int CommandPatch::download(const std::string & sRemoteTgzPath, const std::string & sLocalTgzPath, const std::string & sShortFileName, const std::string& reqMd5, std::string & sResult)
{
    int iRet = 0;

    DownloadTask dtask;
    dtask.sLocalTgzFile   = sLocalTgzPath + FILE_SEP + sShortFileName;
    string sRemoteTgzFile = sRemoteTgzPath + FILE_SEP + sShortFileName;

    NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername
            << ". md5: " << reqMd5
            << ", local: " << dtask.sLocalTgzFile
            << ", remote: " << sRemoteTgzFile
            << ", begin downloading" << endl;

    if(TC_File::isFileExist(dtask.sLocalTgzFile))
    {
        string fileMd5 = TC_MD5::md5file(dtask.sLocalTgzFile);
        if(fileMd5 == reqMd5)
        {
            NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< dtask.sLocalTgzFile << " cached succ" << endl;
            return iRet;
        }
    }

    NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername
            << "|" << reqMd5
            << "|" << dtask.sLocalTgzFile
            << "|" << sRemoteTgzFile
            << "|get lock" << endl;

    TC_ThreadRecLock* taskLock = DownloadTaskFactory::getInstance().getRecLock(dtask);
    bool returned = false;

    {
        TC_ThreadRecLock::Lock lock(*taskLock);

        if(TC_File::isFileExist(dtask.sLocalTgzFile)) //锁内的检查，防止重复下载
        {
            string fileMd5 = TC_MD5::md5file(dtask.sLocalTgzFile);
            if(fileMd5 == reqMd5)
            {
                NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< dtask.sLocalTgzFile << " cached succ" << endl;
                returned = true;
            }
        }

        if(!returned)
        {
            if(!TC_File::isFileExistEx(sLocalTgzPath, S_IFDIR))
            {
                bool mkResult = TC_File::makeDirRecursive(sLocalTgzPath);
                if(!mkResult)
                {
                    string err = TC_Exception::parseError(TC_Exception::getSystemCode());
                    sResult = " mkdir \""+sLocalTgzPath+"\" failure,errno," + err;
                    NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|"<<sResult<< endl;
                    iRet = -1;
                    returned = true;
                }
            }
        }

        if(!returned)
        {
            try
            {
                DownloadEventPtr eventPtr = new PatchDownloadEvent(_serverObjectPtr);

                //到PATCH下载文件
                PatchPrx proxy = Application::getCommunicator()->stringToProxy<PatchPrx>(_patchRequest.patchobj);
                proxy->tars_timeout(60000);

                int downloadRet = SingleFileDownloader::download(proxy, sRemoteTgzFile, dtask.sLocalTgzFile, eventPtr, _patchRequest.appname, _patchRequest.servername, _patchRequest.nodename, sResult);
                if(downloadRet != 0)
                {
                    //返回码错开一下
                    iRet = downloadRet - 100;
                    returned = true;
                }
            }
            catch (std::exception & ex)
            {
                sResult = "download from patch exception," + string(ex.what());
                iRet = -3;
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|" << reqMd5 << "|Exception:" << ex.what() << endl;
                returned = true;
            }
            catch (...)
            {
                sResult = "download from patch error,Unknown Exception";
                iRet = -4;
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|" << reqMd5 << "|Unknown Exception" << endl;
                returned = true;
            }
        }

        if(!returned)
        {
            //检查新下载的文件是否存在
            if(!TC_File::isFileExist(dtask.sLocalTgzFile))
            {
                iRet = -5;
                sResult = "local download file not exist";
                returned = true;
            }
        }

        //检查md5
        if(!returned)
        {
            string fileMd5 = TC_MD5::md5file(dtask.sLocalTgzFile);
            if(fileMd5 != reqMd5)
            {
                iRet = -6;
                sResult = string("request md5:") + reqMd5 + ", local file md5:" + fileMd5 + ", md5 is not equal";
                returned = true;
            }
        }
    } //解锁

    return iRet;
}

inline int CommandPatch::execute(string &sResult)
{
    NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << endl;

    int iRet = -1;
    try
    {
        do
        {
            string sServerName      = _patchRequest.groupname.empty()?_patchRequest.servername:_patchRequest.groupname;
            string sLocalTgzPath    = _localTgzBasePath + FILE_SEP + _patchRequest.appname + "." + sServerName;

            string sShortFile;
            string sRemoteTgzPath;

            // if(_patchRequest.ostype != "")
            // {
            //     //新发布
            //     sShortFile =  _patchRequest.appname + "." + sServerName + "."+ _patchRequest.version + "." + _patchRequest.ostype + ".tgz";
            //     sRemoteTgzPath = FILE_SEP + string("TARSBatchPatchingV2") + FILE_SEP + _patchRequest.appname + FILE_SEP + sServerName;

            //     //使用新路径下载
            //     iRet = download(sRemoteTgzPath, sLocalTgzPath, sShortFile, _patchRequest.md5, sResult);
            //     if(iRet != 0)
            //     {
            //         NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< sRemoteTgzPath << "|" <<  sLocalTgzPath << "|old download error:" << sShortFile
            //                 <<"|"<<sResult << "|" << iRet << endl;
            //         iRet -= 100;
            //     }
            // }
            // else
            // {
                sShortFile = _patchRequest.appname + "." + sServerName + ".tgz";
                sRemoteTgzPath = FILE_SEP + string("TARSBatchPatching") + FILE_SEP + _patchRequest.appname + FILE_SEP + sServerName;
                iRet = download(sRemoteTgzPath, sLocalTgzPath, sShortFile, _patchRequest.md5, sResult);
                if(iRet != 0)
                {
                    NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< sRemoteTgzPath << "|" <<  sLocalTgzPath << "|old download error:" << sShortFile
                            <<"|"<<sResult << "|" << iRet << endl;
                    iRet -= 100;
                    break;
                }
            // }

            string sLocalTgzFile = sLocalTgzPath + FILE_SEP + sShortFile;

            //判断文件是否存在
            if (!TC_File::isFileExist(sLocalTgzFile))
            {
                sResult = sLocalTgzFile + " file not exist";
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< "file not exist:" << sLocalTgzFile << endl;
                iRet = -2;
                break;
            }

            string sLocalExtractPach   = _localExtractBasePath + FILE_SEP + _patchRequest.appname + "." + _patchRequest.servername;
            //如果存在， 则删除之前的已经存在的文件
            if(TC_File::isFileExistEx(sLocalExtractPach, S_IFDIR) && TC_File::removeFile(sLocalExtractPach, true) != 0)
            {
                sResult = " removeFile \""+sLocalExtractPach+"\" failure, error:" + TC_Exception::parseError(TC_Exception::getSystemCode());
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<sResult<< endl;
                iRet = -4;
                break;
            }

            //创建解压目录
            if (!TC_File::makeDirRecursive(sLocalExtractPach))
            {
                sResult = " makeDirRecursive \""+sLocalExtractPach+"\" failure, errnr:" + TC_Exception::parseError(TC_Exception::getSystemCode());
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<sResult<< endl;
                iRet = -5;
                break;
            }

            string busybox;
    #if TARGET_PLATFORM_WINDOWS
            busybox = ServerConfig::TarsPath + string(FILE_SEP) + "tarsnode\\util\\busybox.exe";
    #endif		
            //解压
            TC_Config conf;
            conf.parseFile(_serverObjectPtr->getConfigFile());
            string packageFormat= conf.get("/tars/application/server<packageFormat>","war");
            string cmd,sLocalTgzFile_bak;
            if (_serverObjectPtr->getServerType() == "tars_java") //如果是tars_java，使用war 方法
            {
                //NODE_LOG(_serverObjectPtr->getServerId())->error() <<"tgz file name"<<FILE_FUN<<sLocalTgzFile<<endl;
                if(packageFormat=="jar")
                {
                    sLocalTgzFile_bak=TC_Common::replace(sLocalTgzFile,".tgz",".jar"); 
                    cmd += busybox + " mv " + sLocalTgzFile + " " + sLocalTgzFile_bak + ";";
                }
                else
                {
                    sLocalTgzFile_bak=TC_Common::replace(sLocalTgzFile,".tgz",".war");
                    cmd += busybox + " mv " + sLocalTgzFile +" " + sLocalTgzFile_bak + ";";
                    cmd += busybox + " unzip -oq  " + sLocalTgzFile_bak+ " -d "+ sLocalExtractPach + FILE_SEP +sServerName;
                }
            }
            else
            {
                cmd = busybox + " tar xzfv " + sLocalTgzFile + " -C " + sLocalExtractPach;
            }	

            //解压
            system(cmd.c_str());
            /**
             * 有可能system这里解压失败了，
             * 这里通过遍历解压目录下有没有文件来判断是否解压成功,因为解压之前这个目录是空的
             */
            if(packageFormat!="jar")
            {
                vector<string> files;
                tars::TC_File::listDirectory(sLocalExtractPach, files, true);
                if(files.empty())
                {
                    sResult = cmd + ", error!";
                    NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<sResult<< endl;
                    iRet = -6;
                    break;
                }
            }

            NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "unzip:" << cmd <<endl;

            //移动目录重新命名文件
            if(packageFormat!="jar")
            {
                string sSrcFile     = sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName;
                string sDstFile     = sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + _patchRequest.servername;

                rename(sSrcFile.c_str(), sDstFile.c_str());
                NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< "rename:" << sSrcFile << " " << sDstFile << endl;

                //检查是否需要备份bin目录下的文件夹，针对java服务
                if(backupfiles(sResult) != 0)
                {
                    NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << sResult << endl;
                    iRet = -7;
                    break;
                }
            }

            //先停止服务
            
            // CommandStop stop(_serverObjectPtr, true, true);
            // stop.doProcess(sResult);
            
            string runningDir = _serverObjectPtr->getRunningTmpPath();

            TC_File::removeFile(runningDir, true);

            NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "rename :" <<  _serverObjectPtr->getExePath() << " -> " << runningDir << endl;
            
            rename(_serverObjectPtr->getExePath().c_str(),  runningDir.c_str());

            TC_File::makeDirRecursive(_serverObjectPtr->getExePath());

            // //copy file失败， 会抛异常
            // string patchDir;
            // if (_serverObjectPtr->getServerType() == "tars_nodejs") 
            // { 
            //     // NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName << " -> " <<_serverObjectPtr->getExePath() << endl;
            //     patchDir = sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName;
            //     // rename(sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName,  _serverObjectPtr->getExePath().c_str());
            // }
            // else 
            // { 
            //     if(packageFormat!="jar")
            //     {
            //         // NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << sLocalExtractPach + FILE_SEP + sServerName << " -> " <<_serverObjectPtr->getExePath() << endl;
            //         patchDir = sLocalExtractPach + FILE_SEP + sServerName;
            //         // TC_File::copyFile(sLocalExtractPach + FILE_SEP + sServerName, _serverObjectPtr->getExePath(), true); 
            //     }
            //     else
            //     {
            //         patchDir = sLocalTgzFile_bak;

            //         // string  cpCmd = busybox + " cp -f "+sLocalTgzFile_bak + " " + _serverObjectPtr->getExePath();
            //         // NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << cpCmd << endl;
            //         // system(cpCmd.c_str());
            //     }
            // }

            // rename(patchDir.c_str(),  _serverObjectPtr->getExePath().c_str());

            // NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"rename :" << patchDir << " -> " << _serverObjectPtr->getExePath() << endl;
#if 1            
            // //对于tars_nodejs需要先删除exepath下的文件
            // if (_serverObjectPtr->getServerType() == "tars_nodejs") 
            // { 
            //     if(TC_File::removeFile(_serverObjectPtr->getExePath(),true) != 0 || !TC_File::makeDirRecursive(_serverObjectPtr->getExePath())) 
            //     { 
            //         iRet = -8; //iRetCode = EM_ITEM_PERMISSION;
            //         NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|deal path :"<<_serverObjectPtr->getExePath()<<", error:"<< TC_Exception::parseError(TC_Exception::getSystemCode())<<endl;
            //         break;
            //     } 
            // } 

            //copy file失败， 会抛异常
            if (_serverObjectPtr->getServerType() == "tars_nodejs") 
            { 
                NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName << " -> " <<_serverObjectPtr->getExePath() << endl;

                TC_File::copyFile(sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName, _serverObjectPtr->getExePath(), true); 
            }
            else 
            { 
                if(packageFormat!="jar")
                {
                    NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << sLocalExtractPach + FILE_SEP + sServerName + FILE_SEP + sServerName << " -> " <<_serverObjectPtr->getExePath() << endl;
                    TC_File::copyFile(sLocalExtractPach + FILE_SEP + sServerName, _serverObjectPtr->getExePath(), true); 
                }
                else
                {
                    string  cpCmd = busybox + " cp -f "+sLocalTgzFile_bak + " " + _serverObjectPtr->getExePath();
                    NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<<"|copy :" << cpCmd << endl;
                    system(cpCmd.c_str());
                }
            }
#endif
        } while ( false );

        //设置发布状态到主控
        iRet = updatePatchResult(sResult);
        if(0 != iRet)
        {
            NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|updatePatchResult fail:" << sResult << endl;
        }
        else
        {
            sResult = "patch " + _patchRequest.appname + "." + _patchRequest.servername + " succ, version " + _patchRequest.version;
            //发布成功
            NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< sResult << endl;
        }
        NODE_LOG(_serverObjectPtr->getServerId())->debug() <<FILE_FUN<< "setPatched,iPercent=100%" << endl;
    
    }
    catch (exception& e)
    {
        sResult += e.what();
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Exception:" << e.what() << endl;
        iRet = -8;
    }
    catch (...)
    {
        sResult += "catch unkwon exception";
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Unknown Exception" << endl;
        iRet = -9;
    }

    //这里要求前端判断发布成功的必备条件是bSucc=true
    _serverObjectPtr->setPatchResult(sResult, iRet == 0);

    //设置发布结果,如果是发布异常了这里也设置，主要是设置进度为100%,方便前端判断
    _serverObjectPtr->setPatched(true);

    //发布后，core频率限制重新计算
    _serverObjectPtr->resetCoreInfo();

    g_app.reportServer(_serverObjectPtr->getServerId(), "", _serverObjectPtr->getNodeInfo().nodeName, sResult); 

    return iRet;
}

inline int CommandPatch::backupfiles(std::string &sResult)
{
    try
    {
        if (_serverObjectPtr->getServerType() == "tars_java") //如果是java服务的话需要清空目录，备份目录
        {
            int maxbaknum   = 5;
            string srcPath  = ServerConfig::TarsPath + "tarsnode" + FILE_SEP + "data" + FILE_SEP + _patchRequest.appname + "." + _patchRequest.servername + FILE_SEP;
            string destPath = ServerConfig::TarsPath + "tarsnode" + FILE_SEP + "tmp" + FILE_SEP + _patchRequest.appname + "." + _patchRequest.servername + FILE_SEP;

            if (!TC_File::isFileExistEx(srcPath, S_IFDIR)) //不存在这个目录，先创建
            {
                NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< srcPath << " is not exist,create it... "<<endl;
                TC_File::makeDir(srcPath);
            }

            if (TC_File::makeDir(destPath)) //循环的更新bin_bak目录
            {
                for(unsigned int i = maxbaknum - 1; i >= 1; i--)
                {
					string destPathBak = destPath + "bin_bak" + TC_Common::tostr<int>(i) + FILE_SEP;
                    if(!TC_File::isFileExistEx(destPathBak,S_IFDIR)) //不存在这个目录，可以继续循环
                    {
                        NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< destPathBak << " is not exist,continue... "<<endl;
                        continue;
                    }
                    else //存在这个目录，进行替换:bak1-->bak2
                    {
                        string newFile = destPath + "bin_bak" + TC_Common::tostr<int>(i+1) + FILE_SEP;
                        if(TC_File::isFileExistEx(newFile,S_IFDIR) && TC_File::removeFile(newFile, true) != 0)
                        {
                            string err = TC_Exception::parseError(TC_Exception::getSystemCode());
                            NODE_LOG(_serverObjectPtr->getServerId())->error()  <<FILE_FUN<< "removeFile:"<< newFile << " error,"<< err <<endl;
                        }

                        if(TC_File::makeDir(newFile))
                        {
                            TC_File::copyFile(destPathBak,newFile);
                            NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< "copyFile:"<< destPathBak << " to "<< newFile << " finished!"<<endl;
                        }
                        else
                        {
                            NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< "makeDir:"<< newFile << "error..." <<endl;
                        }
                    }
                }
            }

            //更新当前目录到/bin_bak1/目录
            string existFile    = srcPath + "bin" + FILE_SEP;
            string destPathBak  = destPath + "bin_bak1" + FILE_SEP;

            if(TC_File::isFileExistEx(destPathBak,S_IFDIR) && TC_File::removeFile(destPathBak, true) != 0)
            {
                string err = TC_Exception::parseError(TC_Exception::getSystemCode());
                NODE_LOG(_serverObjectPtr->getServerId())->error()  <<FILE_FUN<< "removeFile:"<< destPathBak << " error,"<< err <<endl;
            }

            if(TC_File::makeDir(destPathBak))
            {
                if(!TC_File::isFileExistEx(existFile,S_IFDIR)) //不存在这个目录，先创建
                {
                    NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< existFile << " backup file is not exist,create it... "<<endl;
                    TC_File::makeDir(existFile);
                }

                TC_File::copyFile(existFile,destPathBak);
                NODE_LOG(_serverObjectPtr->getServerId())->debug()   <<FILE_FUN<< "copyFile:"<< existFile << " to "<< destPathBak << " finished!"<<endl;
                if(TC_File::removeFile(existFile,true) == 0)
                {
                    NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< "removeFile:"<< existFile << " finished!"<<endl;
                    if(TC_File::makeDir(existFile))
                    {
                        NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<< "makeDir:"<< existFile << " finished!"<<endl;
                    }
                }
            }

            //保留指定文件在bin目录中的
            vector<string> vFileNames = TC_Common::sepstr<string>(_serverObjectPtr->getBackupFileNames(), ";|");
            for(vector<string>::size_type i = 0;i< vFileNames.size();i++)
            {
                string sBackupSrc = destPathBak + vFileNames[i];
                string sBackupDest = existFile + vFileNames[i];
                if (TC_File::isFileExistEx(sBackupSrc,S_IFDIR))
                {
                    if (!TC_File::isFileExistEx(sBackupDest,S_IFDIR))
                    {
                        if(!TC_File::makeDirRecursive(TC_File::simplifyDirectory(sBackupDest)))
                        {
                            NODE_LOG(_serverObjectPtr->getServerId())->error()  <<FILE_FUN<< "failed to mkdir dest directory:" << sBackupDest << endl;
                        }
                    }
                    TC_File::copyFile(sBackupSrc,sBackupDest,true);
                    NODE_LOG(_serverObjectPtr->getServerId())->debug()  <<FILE_FUN<<"Backup:"<<sBackupSrc<<" to "<<sBackupDest<<" succ"<<endl;
                }
            }
        }
		else if (_serverObjectPtr->getServerType() == "tars_node") //如果是node服务的话需要清空目录，备份相关文件， 比如配置文件
		{
			int maxbaknum = 5;
			string srcPath = ServerConfig::TarsPath + "tarsnode" + FILE_SEP + "data" + FILE_SEP + _patchRequest.appname + "." + _patchRequest.servername + FILE_SEP;
			string destPath = ServerConfig::TarsPath + "tarsnode" + FILE_SEP + "tmp" + FILE_SEP + _patchRequest.appname + "." + _patchRequest.servername + FILE_SEP;
			string binPath = srcPath + "bin" + FILE_SEP;
			if (!TC_File::isFileExistEx(binPath, S_IFDIR)) //不存在这个目录，先创建
			{
				NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << binPath << " is not exist,create it... " << endl;
				TC_File::makeDir(binPath);
			}
			else
			{
				
				if (!TC_File::makeDir(destPath)) //循环的更新bin_bak目录
				{
					return -1;
				}

				for (unsigned int i = maxbaknum - 1; i >= 1; i--)
				{
					string destPathBak = destPath + "bin_bak" + TC_Common::tostr<int>(i) +FILE_SEP;
					if (!TC_File::isFileExistEx(destPathBak, S_IFDIR)) //不存在这个目录，可以继续循环
					{
						NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << destPathBak << " is not exist,continue... " << endl;
						continue;
					}
					else //存在这个目录，进行替换:bak1-->bak2
					{
						string newFile = destPath + "bin_bak" + TC_Common::tostr<int>(i + 1) + FILE_SEP;
						if (TC_File::isFileExistEx(newFile, S_IFDIR) && TC_File::removeFile(newFile, true) != 0)
						{
							string err = TC_Exception::parseError(TC_Exception::getSystemCode());
							NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "removeFile:" << newFile << " error," << err << endl;
						}

						if (TC_File::renameFile(destPathBak, newFile) == 0)
						{
							NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "renameFile:" << destPathBak << " to " << newFile << " finished!" << endl;
						}
						else
						{
							NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "renameFile:" << destPathBak << " to " << newFile << " fail!" << endl;
						}
					}
				}

				string destPathBak = destPath + "bin_bak1" + FILE_SEP;				
				if (TC_File::renameFile(binPath, destPathBak) == 0)
				{
					NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "renameFile:" << binPath << " to " << destPathBak << " finished!" << endl;
					if (TC_File::makeDir(binPath))
					{
						NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "makedir " << binPath << " finish." << endl;
					}
					else
					{
						NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "makedir " << binPath << " fail." << endl;
						return -1;
					}
				}
				else
				{
					NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "renameFile:" << binPath << " to " << destPathBak << " fail." << endl;
					return -1;
				}

				//保留指定文件在bin目录中的
				vector<string> vFileNames = TC_Common::sepstr<string>(_serverObjectPtr->getBackupFileNames(), ";|");
				for (vector<string>::size_type i = 0; i < vFileNames.size(); i++)
				{
					string sBackupSrc = destPathBak + vFileNames[i];
					string sBackupDest = binPath + vFileNames[i];
					if (TC_File::isFileExistEx(sBackupSrc, S_IFDIR))
					{
						if (!TC_File::isFileExistEx(sBackupDest, S_IFDIR))
						{
							if (!TC_File::makeDirRecursive(TC_File::simplifyDirectory(sBackupDest)))
							{
								NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "failed to mkdir dest directory:" << sBackupDest << endl;
							}
						}
						TC_File::copyFile(sBackupSrc, sBackupDest, true);
						NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "Backup:" << sBackupSrc << " to " << sBackupDest << " succ" << endl;
					}
				}

				// 对于 tarsnode 服务， 还需要保留一些默认文件 bin/*.conf, bin/*.sh, bin/*.bak
				vector<string> oldBinFils;
				TC_File::listDirectory(destPathBak, oldBinFils, false);
				for (vector<string>::size_type i = 0; i < oldBinFils.size(); i++)
				{
					string fName = TC_File::extractFileName(oldBinFils[i]);
					string exName = TC_File::extractFileExt(fName);
					if (exName == "conf" || exName == "sh" || exName == "bak")
					{
						string sBackupSrc = destPathBak + fName;
						string sBackupDest = binPath + fName;
						NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "prepare backup, sBackupSrc:" << sBackupSrc << ", sBackupDest" << sBackupDest << endl;
						if (!TC_File::isFileExistEx(sBackupDest, S_IFDIR))
						{
							if (!TC_File::makeDirRecursive(TC_File::simplifyDirectory(sBackupDest)))
							{
								NODE_LOG(_serverObjectPtr->getServerId())->error() << FILE_FUN << "failed to mkdir dest directory:" << sBackupDest << endl;
							}
						}
						TC_File::copyFile(sBackupSrc, sBackupDest, true);
						NODE_LOG(_serverObjectPtr->getServerId())->debug() << FILE_FUN << "Backup:" << sBackupSrc << " to " << sBackupDest << " succ" << endl;
					}
				}
			}
			
		}
    }
	catch(exception& e)
    {
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Exception:" << e.what() << endl;
        sResult = string(__FUNCTION__) + "|" + _patchRequest.appname + "." + _patchRequest.servername + "|Exception:" + e.what();
        return -1;
    }
	catch(...)
    {
        NODE_LOG(_serverObjectPtr->getServerId())->error() <<FILE_FUN<< _patchRequest.appname + "." + _patchRequest.servername << "|Unknown Exception" << endl;
        sResult = string(__FUNCTION__) + "|" + _patchRequest.appname + "." + _patchRequest.servername + "|Unknown Exception";
        return -1;
    }
    return 0;
}

#endif

