/* Copyright 2013-2017 Sathya Laufer
 *
 * Homegear is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * Homegear is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Homegear.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU Lesser General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
*/

#include "IpcClient.h"
#include "GD/GD.h"

IpcClient::IpcClient(std::string socketPath) : IIpcClient(socketPath)
{
	_localRpcMethods.emplace("influxdbSetLogging", std::bind(&IpcClient::setLogging, this, std::placeholders::_1));
	_localRpcMethods.emplace("broadcastEvent", std::bind(&IpcClient::broadcastEvent, this, std::placeholders::_1));
}

std::string IpcClient::stripNonAlphaNumeric(const std::string& s)
{
	std::string strippedString;
	strippedString.reserve(s.size());
	for(std::string::const_iterator i = s.begin(); i != s.end(); ++i)
	{
		if(isalpha(*i) || isdigit(*i)) strippedString.push_back(*i);
	}
	return strippedString;
}

void IpcClient::onConnect()
{
	try
	{
		bool error = false;

		Ipc::PArray parameters = std::make_shared<Ipc::Array>();
		parameters->reserve(2);

		parameters->push_back(std::make_shared<Ipc::Variable>("influxdbSetLogging"));
		parameters->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tArray)); //Outer array
		Ipc::PVariable signature = std::make_shared<Ipc::Variable>(Ipc::VariableType::tArray); //Inner array (= signature)
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tVoid)); //Return value
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tInteger64)); //1st parameter
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tInteger64)); //2nd parameter
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tString)); //3rd parameter
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tVariant)); //4th parameter
		signature->arrayValue->push_back(std::make_shared<Ipc::Variable>(Ipc::VariableType::tBoolean)); //5th parameter
		parameters->back()->arrayValue->push_back(signature);

		Ipc::PVariable result = invoke("registerRpcMethod", parameters);
		if (result->errorStruct)
		{
			error = true;
			Ipc::Output::printCritical("Critical: Could not register RPC method influxdbSetLogging: " + result->structValue->at("faultString")->stringValue);
		}

		if (error) return;

		GD::out.printInfo("Info: RPC methods successfully registered.");

		load();
	}
	catch (const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (Ipc::IpcException& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

void IpcClient::load()
{
	try
	{
		std::lock_guard<std::mutex> variablesGuard(_variablesMutex);
		std::unordered_map<uint64_t, std::unordered_map<int32_t, std::set<std::string>>> variables = GD::db->getVariables();

		for(auto& peer : variables)
		{
			for(auto& channel : peer.second)
			{
				for(auto& variable : channel.second)
				{
					_variables[peer.first][channel.first].emplace(variable);
				}
			}
		}
	}
	catch (const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (Ipc::IpcException& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
}

// {{{ RPC methods
Ipc::PVariable IpcClient::setLogging(Ipc::PArray& parameters)
{
	try
	{
		if(parameters->size() != 5) return Ipc::Variable::createError(-1, "Wrong parameter count.");
		if(parameters->at(0)->type != Ipc::VariableType::tInteger && parameters->at(0)->type != Ipc::VariableType::tInteger64) return Ipc::Variable::createError(-1, "Parameter 1 is not of type integer.");
		if(parameters->at(1)->type != Ipc::VariableType::tInteger && parameters->at(1)->type != Ipc::VariableType::tInteger64) return Ipc::Variable::createError(-1, "Parameter 2 is not of type integer.");
		if(parameters->at(2)->type != Ipc::VariableType::tString) return Ipc::Variable::createError(-1, "Parameter 3 is not of type string.");
		parameters->at(2)->stringValue = stripNonAlphaNumeric(parameters->at(2)->stringValue);
		if(parameters->at(2)->stringValue.empty()) return Ipc::Variable::createError(-1, "Parameter 3 is an empty string.");
		if(parameters->at(4)->type != Ipc::VariableType::tBoolean) return Ipc::Variable::createError(-1, "Parameter 5 is not of type string.");

		if(parameters->at(4)->booleanValue)
		{
			{ //Check if variable already is being logged
				std::lock_guard<std::mutex> variablesGuard(_variablesMutex);
				auto variablesIterator = _variables.find(parameters->at(0)->integerValue64);
				if(variablesIterator != _variables.end())
				{
					auto channelsIterator = variablesIterator->second.find(parameters->at(1)->integerValue64);
					if(channelsIterator != variablesIterator->second.end())
					{
						auto variableIterator = channelsIterator->second.find(parameters->at(2)->stringValue);
						if(variableIterator != channelsIterator->second.end()) return std::make_shared<Ipc::Variable>();
					}
				}
			}

			GD::db->createVariableTable(parameters->at(0)->integerValue64, parameters->at(1)->integerValue64, parameters->at(2)->stringValue, parameters->at(3));
			std::lock_guard<std::mutex> variablesGuard(_variablesMutex);
			_variables[parameters->at(0)->integerValue64][parameters->at(1)->integerValue64].emplace(parameters->at(2)->stringValue);
		}
		else
		{
			GD::db->deleteVariableTable(parameters->at(0)->integerValue64, parameters->at(1)->integerValue64, parameters->at(2)->stringValue);
			std::lock_guard<std::mutex> variablesGuard(_variablesMutex);
			auto variablesIterator = _variables.find(parameters->at(0)->integerValue64);
			if(variablesIterator != _variables.end())
			{
				auto channelsIterator = variablesIterator->second.find(parameters->at(1)->integerValue64);
				if(channelsIterator != variablesIterator->second.end())
				{
					channelsIterator->second.erase(parameters->at(2)->stringValue);
					if(channelsIterator->second.empty()) variablesIterator->second.erase(channelsIterator);
				}
				if(variablesIterator->second.empty()) _variables.erase(variablesIterator);
			}
		}

		return std::make_shared<Ipc::Variable>();
	}
	catch (const std::exception& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (Ipc::IpcException& ex)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
	}
	catch (...)
	{
		GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
	}
	return Ipc::Variable::createError(-32500, "Unknown application error.");
}

Ipc::PVariable IpcClient::broadcastEvent(Ipc::PArray& parameters)
{
	try
	{
		if(parameters->size() != 4) return Ipc::Variable::createError(-1, "Wrong parameter count.");

		std::lock_guard<std::mutex> variablesGuard(_variablesMutex);
		auto variablesIterator = _variables.find(parameters->at(0)->integerValue64);
		if(variablesIterator != _variables.end())
		{
			auto channelsIterator = variablesIterator->second.find(parameters->at(1)->integerValue64);
			if(channelsIterator != variablesIterator->second.end())
			{
				for(uint32_t i = 0; i < parameters->at(2)->arrayValue->size(); ++i)
				{
					auto variableIterator = channelsIterator->second.find(parameters->at(2)->arrayValue->at(i)->stringValue);
					if(variableIterator != channelsIterator->second.end())
					{
						parameters->at(2)->arrayValue->at(i)->stringValue = stripNonAlphaNumeric(parameters->at(2)->arrayValue->at(i)->stringValue);
						GD::db->saveValue(parameters->at(0)->integerValue64, parameters->at(1)->integerValue64, parameters->at(2)->arrayValue->at(i)->stringValue, parameters->at(3)->arrayValue->at(i));
					}
				}
			}
		}

		return std::make_shared<Ipc::Variable>();
	}
    catch(const std::exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(BaseLib::Exception& ex)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__, ex.what());
    }
    catch(...)
    {
    	GD::out.printEx(__FILE__, __LINE__, __PRETTY_FUNCTION__);
    }
    return Ipc::Variable::createError(-32500, "Unknown application error.");
}
// }}}
