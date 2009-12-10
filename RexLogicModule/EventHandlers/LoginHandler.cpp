// For conditions of distribution and use, see copyright notice in license.txt

#include "StableHeaders.h"
#include "DebugOperatorNew.h"
#include "RexLogicModule.h"
#include "LoginHandler.h"

#include <WorldStream.h>

#include "MemoryLeakCheck.h"
#include <ProtocolModuleOpenSim.h>
#include <OpenSimWorldSession.h>
#include <RealXtendWorldSession.h>
#include <ProtocolModuleTaiga.h>
#include <TaigaWorldSession.h>
#include <Interfaces/LoginCredentialsInterface.h>
#include <Login/LoginCredentials.h>

namespace RexLogic
{
	AbstractLoginHandler::AbstractLoginHandler(Foundation::Framework *framework, RexLogicModule *rex_logic_module)
		: framework_(framework), 
          rex_logic_module_(rex_logic_module), 
          credentials_(0), 
          server_entry_point_url_(0)
	{

	}

	QUrl AbstractLoginHandler::ValidateServerUrl(QString urlString)
	{
		QString sceme = urlString.midRef(0,7).toString();
		if (sceme != "http://")
		{
			urlString.insert(0, "http://");
			rex_logic_module_->LogInfo("http:// was missing from url, corrected");
		}
		QUrl returnUrl(urlString);
		if (returnUrl.isValid())
			return returnUrl;
		else
		{
			rex_logic_module_->LogInfo("invalid url");
			return QUrl();
		}
	}

    void AbstractLoginHandler::Logout()
    {
        rex_logic_module_->LogoutAndDeleteWorld();
    }

    void AbstractLoginHandler::Quit()
    {
        if (rex_logic_module_->GetServerConnection()->IsConnected())
            rex_logic_module_->LogoutAndDeleteWorld();

        framework_->Exit();
    }

	OpenSimLoginHandler::OpenSimLoginHandler(Foundation::Framework *framework, RexLogicModule *rex_logic_module)
		: AbstractLoginHandler(framework, rex_logic_module), opensim_world_session_(0), realxtend_world_session_(0)
	{

	}

	OpenSimLoginHandler::~OpenSimLoginHandler()
	{
		delete credentials_;
    	delete opensim_world_session_;
		delete realxtend_world_session_;
	}

	void OpenSimLoginHandler::InstantiateWorldSession()
	{
		bool success = false;
        QString errorMessage = "";

        ProtocolUtilities::OpenSimCredentials *osCredentials = dynamic_cast<ProtocolUtilities::OpenSimCredentials *>(credentials_);
		if (osCredentials)
		{
			rex_logic_module_->GetServerConnection()->UnregisterCurrentProtocolModule();
			rex_logic_module_->GetServerConnection()->SetCurrentProtocolType(ProtocolUtilities::OpenSim);
			rex_logic_module_->GetServerConnection()->SetConnectionType(ProtocolUtilities::DirectConnection);
			rex_logic_module_->GetServerConnection()->StoreCredentials(osCredentials->GetIdentity().toStdString(),
			                                                         osCredentials->GetPassword().toStdString(),
			                                                         "");
			    
			if ( rex_logic_module_->GetServerConnection()->PrepareCurrentProtocolModule() )
			{	
                SAFE_DELETE(opensim_world_session_);
                // Done deleting sessions/credentials before new try! - Pforce
                assert(!opensim_world_session_); ///<\todo Pforce: Perform proper teardown of previous session to avoid memory leaks.
				opensim_world_session_ = new OpenSimProtocol::OpenSimWorldSession(framework_);
				success = opensim_world_session_->StartSession(osCredentials, &server_entry_point_url_);
				if (success)
				{
					// Save login credentials to config
					if ( framework_->GetConfigManager()->HasKey(std::string("Login"), std::string("server")) )
						framework_->GetConfigManager()->SetSetting<std::string>(std::string("Login"), std::string("server"), server_entry_point_url_.authority().toStdString());
					else
						framework_->GetConfigManager()->DeclareSetting<std::string>(std::string("Login"), std::string("server"), server_entry_point_url_.authority().toStdString());
					if ( framework_->GetConfigManager()->HasKey(std::string("Login"), std::string("username")) )
						framework_->GetConfigManager()->SetSetting<std::string>(std::string("Login"), std::string("username"), osCredentials->GetIdentity().toStdString());
					else
						framework_->GetConfigManager()->DeclareSetting<std::string>(std::string("Login"), std::string("username"), osCredentials->GetIdentity().toStdString());
				}
                else
                    errorMessage = QString(opensim_world_session_->GetConnectionThreadState()->errorMessage.c_str());
			}
		}
		else
		{
			// RealXtend login
            ProtocolUtilities::RealXtendCredentials *rexCredentials = dynamic_cast<ProtocolUtilities::RealXtendCredentials *>(credentials_);
			if (rexCredentials)
			{
				rex_logic_module_->GetServerConnection()->UnregisterCurrentProtocolModule();
				rex_logic_module_->GetServerConnection()->SetCurrentProtocolType(ProtocolUtilities::OpenSim);
				rex_logic_module_->GetServerConnection()->SetConnectionType(ProtocolUtilities::AuthenticationConnection);
			    rex_logic_module_->GetServerConnection()->StoreCredentials(rexCredentials->GetIdentity().toStdString(),
			                                                             rexCredentials->GetPassword().toStdString(),
			                                                             rexCredentials->GetAuthenticationUrl().toString().toStdString());
			    
				if ( rex_logic_module_->GetServerConnection()->PrepareCurrentProtocolModule() )
				{	
                    SAFE_DELETE(realxtend_world_session_);
                    // Done deleting sessions/credentials before new try! - Pforce
                    assert(!realxtend_world_session_); ///<\todo Pforce: Perform proper teardown of previous session to avoid memory leaks.
					realxtend_world_session_ = new OpenSimProtocol::RealXtendWorldSession(framework_);
					success = realxtend_world_session_->StartSession(rexCredentials, &server_entry_point_url_);
					if (success)
					{ 
						// Save login credentials to config
						if ( framework_->GetConfigManager()->HasKey(std::string("Login"), std::string("rex_server")) )
							framework_->GetConfigManager()->SetSetting<std::string>(std::string("Login"), std::string("rex_server"), server_entry_point_url_.authority().toStdString());
						else
							framework_->GetConfigManager()->DeclareSetting<std::string>(std::string("Login"), std::string("rex_server"), server_entry_point_url_.authority().toStdString());
						if ( framework_->GetConfigManager()->HasKey(std::string("Login"), std::string("auth_server")) )
							framework_->GetConfigManager()->SetSetting<std::string>(std::string("Login"), std::string("auth_server"), rexCredentials->GetAuthenticationUrl().authority().toStdString());
						else
							framework_->GetConfigManager()->DeclareSetting<std::string>(std::string("Login"), std::string("auth_server"), rexCredentials->GetAuthenticationUrl().host().toStdString());
						if ( framework_->GetConfigManager()->HasKey(std::string("Login"), std::string("auth_name")) )
							framework_->GetConfigManager()->SetSetting<std::string>(std::string("Login"), std::string("auth_name"), rexCredentials->GetIdentity().toStdString());
						else
							framework_->GetConfigManager()->DeclareSetting<std::string>(std::string("Login"), std::string("auth_name"), rexCredentials->GetIdentity().toStdString());
					}
                    else
                        errorMessage = QString(realxtend_world_session_->GetConnectionThreadState()->errorMessage.c_str());
				}
			}
		}
	}

	void OpenSimLoginHandler::ProcessOpenSimLogin(QMap<QString,QString> map)
	{
        SAFE_DELETE(credentials_);
        // Done deleting sessions/credentials before new try! - Pforce
        assert(!credentials_); ///<\todo Pforce: Perform proper teardown of previous credentials to avoid memory leaks.
		credentials_ = new ProtocolUtilities::OpenSimCredentials();
		ProtocolUtilities::OpenSimCredentials *osCredentials = dynamic_cast<ProtocolUtilities::OpenSimCredentials *>(credentials_);
		if (osCredentials)
		{
			QString username = map["Username"];
			QStringList firstAndLast = username.split(" ");
			if (firstAndLast.length() == 2)
			{
				osCredentials->SetFirstName(firstAndLast.at(0));
				osCredentials->SetLastName(firstAndLast.at(1));
				osCredentials->SetPassword(map["Password"]);
				server_entry_point_url_ = ValidateServerUrl(map["WorldAddress"]);
				if (server_entry_point_url_.isValid())
                {
                    emit( LoginStarted() );
					InstantiateWorldSession();
                }
			}
			else
			{
			    rex_logic_module_->LogInfo("Username was not in form firstname lastname, could not perform login");
		    }		
		}
	}

	void OpenSimLoginHandler::ProcessRealXtendLogin(QMap<QString,QString> map)
	{
        SAFE_DELETE(credentials_);
        // Done deleting sessions/credentials before new try! - Pforce
        assert(!credentials_); ///<\todo Pforce: Perform proper teardown of previous credentials to avoid memory leaks.
		credentials_ = new ProtocolUtilities::RealXtendCredentials();
		ProtocolUtilities::RealXtendCredentials *rexCredentials = dynamic_cast<ProtocolUtilities::RealXtendCredentials *>(credentials_);
		if (rexCredentials)
		{
			rexCredentials->SetIdentity(map["Username"]);
			rexCredentials->SetPassword(map["Password"]);
			rexCredentials->SetAuthenticationUrl(ValidateServerUrl(map["AuthenticationAddress"]));
			server_entry_point_url_ = ValidateServerUrl(map["WorldAddress"]);
			if (server_entry_point_url_.isValid())
            {
                emit( LoginStarted() );
				InstantiateWorldSession();
            }
		}
	}


	TaigaLoginHandler::TaigaLoginHandler(Foundation::Framework *framework, RexLogicModule *rex_logic_module)
		: AbstractLoginHandler(framework, rex_logic_module), taiga_world_session_(0)
	{
        credentials_ = new ProtocolUtilities::TaigaCredentials();
	}

	TaigaLoginHandler::~TaigaLoginHandler()
	{
		delete credentials_;
		delete taiga_world_session_;
	}

	void TaigaLoginHandler::InstantiateWorldSession()
	{
		bool success = false;
        QString errorMessage = "";

		rex_logic_module_->GetServerConnection()->UnregisterCurrentProtocolModule();
		rex_logic_module_->GetServerConnection()->SetCurrentProtocolType(ProtocolUtilities::Taiga);
		rex_logic_module_->GetServerConnection()->SetConnectionType(ProtocolUtilities::DirectConnection);
		ProtocolUtilities::TaigaCredentials *tgCredentials = dynamic_cast<ProtocolUtilities::TaigaCredentials *>(credentials_);	
		if (tgCredentials)	    
    	{
    		rex_logic_module_->GetServerConnection()->StoreCredentials(
    		    tgCredentials->GetIdentity().toStdString(),
    		    "",
    		    "");
        }
        if ( rex_logic_module_->GetServerConnection()->PrepareCurrentProtocolModule() )
		{
            SAFE_DELETE(taiga_world_session_);
            // Done deleting sessions/credentials before new try! - Pforce
            assert(!taiga_world_session_); ///<\todo Pforce: Perform proper teardown of previous session to avoid memory leaks.
			taiga_world_session_ = new TaigaProtocol::TaigaWorldSession(framework_);
			success = taiga_world_session_->StartSession(credentials_, &server_entry_point_url_);
            if (!success)
                errorMessage = QString(taiga_world_session_->GetConnectionThreadState()->errorMessage.c_str());
		}
	}

	void TaigaLoginHandler::ProcessCommandParameterLogin(QString &entry_point_url)
	{
		server_entry_point_url_ = ValidateServerUrl(entry_point_url);
		dynamic_cast<ProtocolUtilities::TaigaCredentials *>(credentials_)->SetIdentityUrl("NotNeeded");
		if (server_entry_point_url_.isValid())
		{
			emit( LoginStarted() );
			InstantiateWorldSession();
		}
	}

	void TaigaLoginHandler::ProcessWebLogin(QWebFrame *web_frame)
	{
		int pos1, pos2;
		QString entry_point_url, identityUrl;
		QString returnValue = web_frame->evaluateJavaScript("ReturnSuccessValue()").toString();

		pos1 = returnValue.indexOf(QString("http://"), 0);
		pos2 = returnValue.indexOf(QString("?"), 0);
		entry_point_url = returnValue.midRef(pos1, pos2-pos1).toString();

		pos1 = returnValue.lastIndexOf(QString("&"));
		identityUrl = returnValue.midRef(pos1+1, returnValue.length()-1).toString();
		
		dynamic_cast<ProtocolUtilities::TaigaCredentials *>(credentials_)->SetIdentityUrl(identityUrl);
		server_entry_point_url_ = ValidateServerUrl(entry_point_url);
		if (server_entry_point_url_.isValid())
        {
            emit( LoginStarted() );
			InstantiateWorldSession();
        }
	}
}