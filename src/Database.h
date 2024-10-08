/* Copyright 2013-2019 Homegear GmbH
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

#ifndef DATABASECONTROLLER_H_
#define DATABASECONTROLLER_H_

#include "homegear-base/BaseLib.h"
#include "homegear-ipc/Variable.h"
#include "homegear-ipc/JsonDecoder.h"
#include "homegear-ipc/JsonEncoder.h"

class Database : public BaseLib::IQueue {
 public:
  class QueueEntry : public BaseLib::IQueueEntry {
   public:
    QueueEntry(const std::string &command, bool lowres) {
      _command = command;
      _lowres = lowres;
    };
    ~QueueEntry() override = default;;
    std::string &getCommand() { return _command; }
    bool getLowres() const { return _lowres; }
   private:
    std::string _command;
    bool _lowres = false;
  };

  explicit Database(BaseLib::SharedObjects *bl);
  ~Database() override;

  // {{{ General
  bool open();
  // }}}

  // {{{ History
  std::unordered_map<uint64_t, std::unordered_map<int32_t, std::set<std::string>>> getVariables();
  void deleteVariableTable(uint64_t peerId, int32_t channel, std::string variable);
  void createVariableTable(uint64_t peerId, int32_t channel, std::string variable, Ipc::PVariable initialValue);
  void saveValue(uint64_t peerId, int32_t channel, std::string &variable, Ipc::PVariable value);
  Ipc::PVariable influxQueryPost(const std::string &query, const std::string &additionalHttpQueryStringParameters);
  Ipc::PVariable influxQueryGet(const std::string &query, const std::string &additionalHttpQueryStringParameters);
  Ipc::PVariable influxWrite(const std::string &query, bool lowRes);
  Ipc::PVariable queueInfluxWrite(const std::string &query, bool low_res);
  Ipc::PVariable createContinuousQuery(const std::string &measurement);
  // }}}
 protected:
  std::atomic_bool _initializing{false};
  std::string _credentials;
  std::string _pingHeader;
  std::string _writeHeader;
  std::string _writeHeaderLowRes;
  std::unique_ptr<BaseLib::HttpClient> _httpClient;
  std::unique_ptr<Ipc::JsonDecoder> _jsonDecoder;
  std::unique_ptr<Ipc::JsonEncoder> _jsonEncoder;

  static std::string getTableName(uint64_t peerId, int32_t channel, std::string &variable);
  void processQueueEntry(int32_t index, std::shared_ptr<BaseLib::IQueueEntry> &entry) override;
};

#endif
