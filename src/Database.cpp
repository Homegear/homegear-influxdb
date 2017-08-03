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

#include "Database.h"
#include "GD/GD.h"

Database::Database(BaseLib::SharedObjects* bl) : IQueue(bl, 1, 100000)
{
	_httpClient.reset(new BaseLib::HttpClient(_bl, GD::settings.hostname(), GD::settings.port()));
	_jsonDecoder = std::unique_ptr<Ipc::JsonDecoder>(new Ipc::JsonDecoder());
	_jsonEncoder = std::unique_ptr<Ipc::JsonEncoder>(new Ipc::JsonEncoder());

	startQueue(0, false, 1, 0, SCHED_OTHER);
}

Database::~Database()
{
	stopQueue(0);
}

std::string Database::getTableName(uint64_t peerId, int32_t channel, std::string& variable)
{
	std::string channelString = (channel < 0 ? "n" : std::to_string(channel));
	return "history_" + std::to_string(peerId) + "_" + channelString + "_" + variable;
}

//{{{ General
	Ipc::PVariable Database::influxQueryGet(std::string query)
	{
		//No try/catch to throw error in calling method
		std::string request = "GET /query?db=" + BaseLib::Http::encodeURL(GD::settings.databaseName()) + "&q=" + BaseLib::Http::encodeURL(query) + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nConnection: Close\r\n\r\n";
		BaseLib::Http response;
		_httpClient->sendRequest(request, response, false);
		Ipc::PVariable result;
		if(response.getContentSize() > 0) result = _jsonDecoder->decode(response.getContent());
		else result.reset(new Ipc::Variable());
		if(response.getHeader().responseCode < 200 || response.getHeader().responseCode > 299)
		{
			GD::out.printError("Error: Code " + std::to_string(response.getHeader().responseCode) + " received in response to query " + query + ". Response content: " + std::string(response.getContent().data(), response.getContentSize()));
			if(result->structValue->find("error") != result->structValue->end()) result = Ipc::Variable::createError(-1, result->structValue->at("error")->stringValue);
		}
		return result;
	}

	Ipc::PVariable Database::influxQueryPost(std::string query)
	{
		//No try/catch to throw error in calling method
		std::string encodedQuery = BaseLib::Http::encodeURL(query);
		std::string request = _queryPostHeader + "Content-Length: " + std::to_string(encodedQuery.size() + 2) + "\r\n\r\nq=" + encodedQuery;
		BaseLib::Http response;
		_httpClient->sendRequest(request, response, false);
		Ipc::PVariable result;
		if(response.getContentSize() > 0) result = _jsonDecoder->decode(response.getContent());
		else result.reset(new Ipc::Variable());
		if(response.getHeader().responseCode < 200 || response.getHeader().responseCode > 299)
		{
			GD::out.printError("Error: Code " + std::to_string(response.getHeader().responseCode) + " received in response to query " + query + ". Response content: " + std::string(response.getContent().data(), response.getContentSize()));
			if(result->structValue->find("error") != result->structValue->end()) result = Ipc::Variable::createError(-1, result->structValue->at("error")->stringValue);
		}
		return result;
	}

	Ipc::PVariable Database::influxWrite(std::string query, bool lowRes)
	{
		BaseLib::Http response;
		try
		{
			std::string request = (lowRes? _writeHeaderLowRes : _writeHeader) + "Content-Length: " + std::to_string(query.size()) + "\r\n\r\n" + query;
			_httpClient->sendRequest(request, response, false);
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
		if(response.getContentSize() != 0 && response.getHeader().responseCode > 399)
		{
			Ipc::PVariable result = _jsonDecoder->decode(response.getContent());
			if(result->structValue->find("error") != result->structValue->end()) result = Ipc::Variable::createError(-1, result->structValue->at("error")->stringValue);
			return result;
		}
		return Ipc::PVariable();
	}

	bool Database::open()
	{
		try
		{
			_httpClient.reset(new BaseLib::HttpClient(_bl, GD::settings.hostname(), GD::settings.port(), false));
			_pingHeader = "HEAD /ping HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nConnection: Close\r\n\r\n";
			_queryPostHeader = "POST /query HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nContent-Type: application/x-www-form-urlencoded\r\nConnection: Close\r\n";

			BaseLib::Http response;
			_httpClient->sendRequest(_pingHeader, response, true);

			auto versionIterator = response.getHeader().fields.find("x-influxdb-version");
			std::string version;
			if(versionIterator != response.getHeader().fields.end()) version = versionIterator->second;
			else version = "Unknown";
			GD::out.printInfo("Info: Successfully connected to InfluxDB. Response code to ping request was: " + std::to_string(response.getHeader().responseCode) + ". InfluxDB version: " + version);

			Ipc::PVariable result = influxQueryPost("CREATE DATABASE \"" + GD::settings.databaseName() + "\"");
			if(result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Database \"" + GD::settings.databaseName() + "\" exists or was created successfully.");
			else GD::out.printError("Error: Unknown response received to \"CREATE DATABASE\".");

			result = influxQueryPost("CREATE RETENTION POLICY \"highres\" ON \"" + GD::settings.databaseName() + "\" DURATION " + std::to_string(GD::settings.highResolutionRetentionTime()) + "d REPLICATION 1 DEFAULT");
			if(result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Successfully created high resolution retention policy, if it didn't previously exist.");
			else GD::out.printError("Error: Unknown response received to \"CREATE RETENTION POLICY\".");

			result = influxQueryPost("ALTER RETENTION POLICY \"highres\" ON \"" + GD::settings.databaseName() + "\" DURATION " + std::to_string(GD::settings.highResolutionRetentionTime()) + "d DEFAULT");
			if(result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Successfully updated high resolution retention policy.");
			else GD::out.printError("Error: Unknown response received to \"ALTER RETENTION POLICY\".");

			result = influxQueryPost("CREATE RETENTION POLICY \"lowres\" ON \"" + GD::settings.databaseName() + "\" DURATION " + std::to_string(GD::settings.lowResolutionRetentionTime()) + "d REPLICATION 1");
			if(result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Successfully created low resolution retention policy, if it didn't previously exist.");
			else GD::out.printError("Error: Unknown response received to \"CREATE RETENTION POLICY\".");

			result = influxQueryPost("ALTER RETENTION POLICY \"lowres\" ON \"" + GD::settings.databaseName() + "\" DURATION " + std::to_string(GD::settings.lowResolutionRetentionTime()) + "d");
			if(result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Successfully updated low resolution retention policy.");
			else GD::out.printError("Error: Unknown response received to \"ALTER RETENTION POLICY\".");

			_queryPostHeader = "POST /query?db=" + BaseLib::Http::encodeURL(GD::settings.databaseName()) + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nContent-Type: application/x-www-form-urlencoded\r\nConnection: Close\r\n";
			_writeHeader = "POST /write?db=" + BaseLib::Http::encodeURL(GD::settings.databaseName()) + " HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nConnection: Close\r\n";
			_writeHeaderLowRes = "POST /write?db=" + BaseLib::Http::encodeURL(GD::settings.databaseName()) + "&rp=lowres HTTP/1.1\r\nUser-Agent: Homegear\r\nHost: " + GD::settings.hostname() + ":" + std::to_string(GD::settings.port()) + "\r\nConnection: Close\r\n";
			return true;
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
		return false;
	}

	void Database::processQueueEntry(int32_t index, std::shared_ptr<BaseLib::IQueueEntry>& entry)
	{
		std::shared_ptr<QueueEntry> queueEntry = std::dynamic_pointer_cast<QueueEntry>(entry);
		if(!queueEntry) return;

		Ipc::PVariable result = influxWrite(queueEntry->getCommand(), false);
		if(result && result->errorStruct) GD::out.printError("Error creating measurement (1): " + result->structValue->at("faultString")->stringValue);

		if(queueEntry->getLowres())
		{
			result = influxWrite(queueEntry->getCommand(), true);
			if(result && result->errorStruct) GD::out.printError("Error creating measurement (2): " + result->structValue->at("faultString")->stringValue);
		}
	}
//}}}

//{{{ History
	std::unordered_map<uint64_t, std::unordered_map<int32_t, std::set<std::string>>> Database::getVariables()
	{
		try
		{
			std::unordered_map<uint64_t, std::unordered_map<int32_t, std::set<std::string>>> variables;

			Ipc::PVariable result = influxQueryPost("SHOW MEASUREMENTS ON \"" + GD::settings.databaseName() + "\"");
			auto resultsIterator = result->structValue->find("results");
			if(resultsIterator != result->structValue->end() && !resultsIterator->second->arrayValue->empty())
			{
				auto seriesIterator = resultsIterator->second->arrayValue->at(0)->structValue->find("series");
				if(seriesIterator != resultsIterator->second->arrayValue->at(0)->structValue->end() && !seriesIterator->second->arrayValue->empty())
				{
					auto valuesIterator = seriesIterator->second->arrayValue->at(0)->structValue->find("values");
					if(valuesIterator != seriesIterator->second->arrayValue->at(0)->structValue->end())
					{
						for(auto& row : *valuesIterator->second->arrayValue)
						{
							for(auto& col : *row->arrayValue)
							{
								std::vector<std::string> fields = BaseLib::HelperFunctions::splitAll(col->stringValue, '_');
								if(fields.size() != 4 || fields.at(1).empty() || fields.at(2).empty() || fields.at(3).empty()) continue;
								uint64_t peerId = BaseLib::Math::getNumber64(fields.at(1));
								int32_t channel = fields.at(2) == "n" ? -1 : BaseLib::Math::getNumber(fields.at(2));
								variables[peerId][channel].emplace(fields.at(3));
							}
						}
					}
				}
			}

			return variables;
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
		return std::unordered_map<uint64_t, std::unordered_map<int32_t, std::set<std::string>>>();
	}

	void Database::deleteVariableTable(uint64_t peerId, int32_t channel, std::string variable)
	{
		std::string tableName = getTableName(peerId, channel, variable);

		influxQueryPost("DROP CONTINUOUS QUERY \"cq_" + tableName + "\" ON \"" + GD::settings.databaseName() + "\"");

		influxQueryPost("DROP MEASUREMENT \"" + tableName + "\"");
	}

	void Database::createVariableTable(uint64_t peerId, int32_t channel, std::string variable, Ipc::PVariable initialValue)
	{
		GD::out.printInfo("Info: Creating measurement for variable: PeerId " + std::to_string(peerId) + ", channel " + std::to_string(channel) + ", variable " + variable + ", type " + initialValue->getTypeString(initialValue->type));

		std::string tableName = getTableName(peerId, channel, variable);

		if(initialValue->type != Ipc::VariableType::tBoolean && initialValue->type != Ipc::VariableType::tFloat && initialValue->type != Ipc::VariableType::tInteger && initialValue->type != Ipc::VariableType::tInteger64 && initialValue->type != Ipc::VariableType::tString && initialValue->type != Ipc::VariableType::tBase64)
		{
			Ipc::PVariable encodedValue = std::make_shared<Ipc::Variable>(Ipc::VariableType::tString);
			encodedValue->stringValue = _jsonEncoder->getString(initialValue);
			initialValue = encodedValue;
		}

		Ipc::PVariable result = influxWrite(tableName + " value=" + (initialValue->type == Ipc::VariableType::tString ? "\"" : "") + initialValue->toString() + (initialValue->type == Ipc::VariableType::tString ? "\"" : ""), false);
		if(result && result->errorStruct) GD::out.printError("Error creating measurement: " + result->structValue->at("faultString")->stringValue);

		if(initialValue->type == Ipc::VariableType::tFloat || initialValue->type == Ipc::VariableType::tInteger || initialValue->type == Ipc::VariableType::tInteger64)
		{
			result = influxQueryPost("CREATE CONTINUOUS QUERY \"cq_" + tableName + "\" ON \"" + GD::settings.databaseName() + "\" BEGIN SELECT mean(value) AS value,min(value) AS value_min,max(value) AS value_max INTO \"lowres\".\"" + tableName + "\" FROM \"" + tableName + "\" GROUP BY time(30m) END");
			if(result && result->structValue->find("results") != result->structValue->end()) GD::out.printInfo("Info: Continuous query was created successfully.");
			else GD::out.printError("Error: Unknown response received to \"CREATE CONTINUOUS QUERY\".");
		}
	}

	void Database::saveValue(uint64_t peerId, int32_t channel, std::string& variable, Ipc::PVariable value)
	{
		if(variable.empty() || !value) return;

		std::string tableName = getTableName(peerId, channel, variable);

		if(value->type != Ipc::VariableType::tBoolean && value->type != Ipc::VariableType::tFloat && value->type != Ipc::VariableType::tInteger && value->type != Ipc::VariableType::tInteger64 && value->type != Ipc::VariableType::tString && value->type != Ipc::VariableType::tBase64)
		{
			Ipc::PVariable encodedValue = std::make_shared<Ipc::Variable>(Ipc::VariableType::tString);
			encodedValue->stringValue = _jsonEncoder->getString(value);
			value = encodedValue;
		}

		std::shared_ptr<BaseLib::IQueueEntry> entry = std::make_shared<QueueEntry>(tableName + " value=" + (value->type == Ipc::VariableType::tString ? "\"" : "") + value->toString() + (value->type == Ipc::VariableType::tString ? "\"" : ""), value->type != Ipc::VariableType::tFloat && value->type != Ipc::VariableType::tInteger && value->type != Ipc::VariableType::tInteger64);
		enqueue(0, entry);
	}
//}}}
