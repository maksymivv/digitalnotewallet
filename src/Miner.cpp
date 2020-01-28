// Copyright (c) 2012-2016, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2016-2019, The Karbo developers
//
// This file is part of Karbo.
//
// Karbo is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Karbo is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Karbo.  If not, see <http://www.gnu.org/licenses/>.

#include "Miner.h"

#include <future>
#include <numeric>
#include <sstream>
#include <thread>
#include <QDebug>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/limits.hpp>
#include <boost/utility/value_init.hpp>

#include <CryptoNoteConfig.h>

#include "crypto/crypto.h"
#include "crypto/cn_slow_hash.hpp"
#include "crypto/random.h"
#include "Common/CommandLine.h"
#include "Common/Math.h"
#include "Common/StringTools.h"
#include "Serialization/SerializationTools.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/TransactionExtra.h"

#include "CurrencyAdapter.h"
#include "Wallet/WalletRpcServerCommandsDefinitions.h"

#include "NodeAdapter.h"

#include <QThread>
#include <QTimerEvent>

using namespace Logging;
using namespace CryptoNote;

namespace WalletGui
{

  Miner::Miner(QObject* _parent) :
    QObject(_parent),
    m_stop_mining(true),
    m_template(boost::value_initialized<Block>()),
    m_template_no(0),
    m_diffic(0),
    m_stake_amount(0),
    m_stake_mixin(3),
    m_stake_term(0),
    m_pausers_count(0),
    m_threads_total(0),
    m_starter_nonce(0),
    m_last_hr_merge_time(0),
    m_hashes(0),
    m_do_mining(false),
    m_current_hash_rate(0),
    m_update_block_template_interval(30),
    m_update_merge_hr_interval(2) {
  }
  //-----------------------------------------------------------------------------------------------------
  Miner::~Miner() {
    stop();
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::set_block_template(const Block& bl, const difficulty_type& di) {
    std::lock_guard<decltype(m_template_lock)> lk(m_template_lock);

    m_template = bl;

    if (m_template.majorVersion == BLOCK_MAJOR_VERSION_2 || m_template.majorVersion == BLOCK_MAJOR_VERSION_3) {
      CryptoNote::TransactionExtraMergeMiningTag mm_tag;
      mm_tag.depth = 0;
      if (!CryptoNote::get_aux_block_header_hash(m_template, mm_tag.merkleRoot)) {
        return false;
      }

      m_template.parentBlock.baseTransaction.extra.clear();
      if (!CryptoNote::appendMergeMiningTagToExtra(m_template.parentBlock.baseTransaction.extra, mm_tag)) {
        return false;
      }
    }

    m_diffic = di;
    ++m_template_no;
    m_starter_nonce = Random::randomValue<uint32_t>();
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::on_block_chain_update() {
    if (!is_mining()) {
      return true;
    }

    if (request_block_template()) {
      resume();
      return true;
    }

    return false;
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::request_block_template() {
    qDebug() << "Requesting block template";
    Block bl = boost::value_initialized<Block>();
    CryptoNote::difficulty_type di = 0;
    uint32_t height;
    CryptoNote::BinaryArray extra_nonce;

    uint64_t fee = 0;
    size_t median_size = 0;
    size_t txs_size = 0;
    uint64_t already_generated_coins = 0;
    uint64_t reward = 0;
    int64_t emission_change = 0;
    Crypto::SecretKey stakeKey;

    uint64_t actualBalance = WalletAdapter::instance().getActualBalance();

    // get block template without coinbase tx
    if (!NodeAdapter::instance().prepareBlockTemplate(bl, fee, m_mine_address, di, height, extra_nonce, median_size, txs_size, already_generated_coins)) {
      qDebug() << "Failed to get_block_template(), stopping mining";
      Q_EMIT minerMessageSignal(QString("Failed to get_block_template()"));
      return false;
    }

    if (!CurrencyAdapter::instance().getCurrency().getBlockReward(bl.majorVersion, median_size, txs_size, already_generated_coins, fee, reward, emission_change)) {
      qDebug() << "Failed to calculate block reward, stopping mining";
      Q_EMIT minerMessageSignal(QString("Failed to get block reward"));
      return false;
    }

    // get base stake amount
    uint64_t base_stake = NodeAdapter::instance().getBaseStake();

    if (actualBalance < m_stake_amount) {
      qDebug() << "Not enough balance for stake";
      Q_EMIT minerMessageSignal(QString("Not enough balance for stake"));
      return false;
    }

    // calculate term here
    m_stake_term = CurrencyAdapter::instance().getCurrency().calculateStakeDepositTerm(base_stake, m_stake_amount);

    // now get stake tx from wallet
    if (!WalletAdapter::instance().getStakeTransaction(m_mine_address_str,
                                                       m_stake_amount,
                                                       reward,
                                                       m_stake_mixin,
                                                       height + m_stake_term,
                                                       "",
                                                       bl.baseTransaction,
                                                       stakeKey)) {
      qDebug() << "Failed to getStakeTransaction(), stopping mining";
      Q_EMIT minerMessageSignal(QString("Failed to getStakeTransaction()"));
      return false;
    }

    // adjust difficulty by stake
    difficulty_type adj_d = CurrencyAdapter::instance().getCurrency().calculateStakeDifficulty(di, base_stake, m_stake_amount);

    set_block_template(bl, adj_d);

    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::on_idle()
  {
    m_update_block_template_interval.call([&](){
      if (is_mining())
        request_block_template();
      return true;
    });

    m_update_merge_hr_interval.call([&](){
      merge_hr();
      return true;
    });

    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  uint64_t millisecondsSinceEpoch() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  }

  //-----------------------------------------------------------------------------------------------------
  void Miner::merge_hr()
  {
    if(m_last_hr_merge_time && is_mining()) {
      m_current_hash_rate = m_hashes * 1000 / (millisecondsSinceEpoch() - m_last_hr_merge_time + 1);
      std::lock_guard<std::mutex> lk(m_last_hash_rates_lock);
      m_last_hash_rates.push_back(m_current_hash_rate);
      if(m_last_hash_rates.size() > 19)
        m_last_hash_rates.pop_front();

      uint64_t total_hr = std::accumulate(m_last_hash_rates.begin(), m_last_hash_rates.end(), static_cast<uint64_t>(0));
      float hr = static_cast<float>(total_hr)/static_cast<float>(m_last_hash_rates.size())/static_cast<float>(1000);
      qDebug() << "Hashrate: " /*<< std::setprecision(2) << std::fixed*/ << hr << " kH/s";
    }
    
    m_last_hr_merge_time = millisecondsSinceEpoch();
    m_hashes = 0;
  }

  //-----------------------------------------------------------------------------------------------------
  bool Miner::is_mining()
  {
    return !m_stop_mining;
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::start(const std::string& address, size_t threads_count, uint64_t stake)
  {   
    if (!m_stop_mining) {
      qDebug() << "Starting miner but it's already started";
      return false;
    }

    std::lock_guard<std::mutex> lk(m_threads_lock);

    if(!m_threads.empty()) {
      qDebug() << "Unable to start miner because there are active mining threads";
      return false;
    }

    m_mine_address = CurrencyAdapter::instance().internalAddress(QString::fromStdString(address));
    m_mine_address_str = address;

    m_threads_total = static_cast<uint32_t>(threads_count);
    m_starter_nonce = Random::randomValue<uint32_t>();

    // always request block template on start
    if (!request_block_template()) {
      qDebug() << "Unable to start miner because block template request was unsuccessful";
      return false;
    }

    m_stop_mining = false;
    m_pausers_count = 0; // in case mining wasn't resumed after pause

    for (uint32_t i = 0; i != threads_count; i++) {
      m_threads.push_back(std::thread(std::bind(&Miner::worker_thread, this, i)));
    }

    qDebug() << "Mining has started with " << threads_count << " thread(s), at difficulty " << m_diffic << " good luck!";
    Q_EMIT minerMessageSignal(QString("Mining has started with %1 thread(s) at difficulty %2, good luck!").arg(threads_count).arg(m_diffic));
    return true;
  }
  
  //-----------------------------------------------------------------------------------------------------
  uint64_t Miner::get_speed()
  {
    if(is_mining())
      return m_current_hash_rate;
    else
      return 0;
  }
  
  //-----------------------------------------------------------------------------------------------------
  void Miner::send_stop_signal() 
  {
    m_stop_mining = true;
  }

  //-----------------------------------------------------------------------------------------------------
  bool Miner::stop()
  {
    send_stop_signal();

    std::lock_guard<std::mutex> lk(m_threads_lock);

    for (auto& th : m_threads) {
      th.detach();
    }

    m_threads.clear();
    qDebug() << "Mining stopped, " << m_threads.size() << " finished" ;
    Q_EMIT minerMessageSignal(QString("Mining stopped, %1 finished").arg(m_threads.size()));
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  void Miner::on_synchronized()
  {
    if(m_do_mining) {
      start(m_mine_address_str, m_threads_total, m_stake_amount);
    }
  }
  //-----------------------------------------------------------------------------------------------------
  void Miner::pause()
  {
    std::lock_guard<std::mutex> lk(m_miners_count_lock);
    ++m_pausers_count;
    if(m_pausers_count == 1 && is_mining())
      qDebug() << "MINING PAUSED";
  }
  //-----------------------------------------------------------------------------------------------------
  void Miner::resume()
  {
    std::lock_guard<std::mutex> lk(m_miners_count_lock);
    --m_pausers_count;
    if(m_pausers_count < 0)
    {
      m_pausers_count = 0;
      qDebug() << "Unexpected Miner::resume() called";
      //Q_EMIT minerMessageSignal(QString("Unexpected Miner::resume() called"));
    }
    if(!m_pausers_count && is_mining())
      qDebug() << "MINING RESUMED";
      //Q_EMIT minerMessageSignal(QString("MINING RESUMED"));
  }
  //-----------------------------------------------------------------------------------------------------
  bool Miner::worker_thread(uint32_t th_local_index)
  {
    qDebug() << "Miner thread was started ["<< th_local_index << "]";
    uint32_t nonce = m_starter_nonce + th_local_index;
    difficulty_type local_diff = 0;
    uint32_t local_template_ver = 0;
    cn_pow_hash_v2 hash_ctx;
    Block b;

    while(!m_stop_mining)
    {
      if(m_pausers_count) //anti split workaround
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      if(local_template_ver != m_template_no) {
        std::unique_lock<std::mutex> lk(m_template_lock);
        b = m_template;
        local_diff = m_diffic;
        lk.unlock();

        local_template_ver = m_template_no;
        nonce = m_starter_nonce + th_local_index;
      }

      if(!local_template_ver) //no any set_block_template call
      {
        qDebug() << "Block template not set yet";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }

      b.nonce = nonce;
      Crypto::Hash h;
      if (!m_stop_mining && !get_block_longhash(hash_ctx, b, h)) {
        qDebug() << "Failed to get block long hash";
        m_stop_mining = true;
      }

      if (!m_stop_mining && check_hash(h, local_diff))
      {
        //we lucky!

        pause();

        Crypto::Hash id;
        if (!get_block_hash(b, id)) {
          qDebug() << "Failed to get mined block hash";
        }
 
        qDebug() << "Found block for difficulty: " << local_diff;
        Q_EMIT minerMessageSignal(QString("Found block %1 at height %2 for difficulty %3, POW %4").arg(QString::fromStdString(Common::podToHex(id))).arg(b.blockIndex).arg(local_diff).arg(QString::fromStdString(Common::podToHex(h))));

        if(!NodeAdapter::instance().handleBlockFound(b)) {
          qDebug() << "Failed to submit block";
          Q_EMIT minerMessageSignal(QString("Failed to submit block"));
        } else {
          // yay!
        }
      }

      nonce += m_threads_total;
      ++m_hashes;
    }
    qDebug() << "Miner thread stopped ["<< th_local_index << "]";
    return true;
  }

  void Miner::stakeAmountChanged(uint64_t _amount) {
    m_stake_amount = _amount;
    qDebug() << "Stake amount changed to " << m_stake_amount;

    uint64_t base_stake = NodeAdapter::instance().getBaseStake();
    difficulty_type baseDiff = NodeAdapter::instance().getDifficulty();
    m_diffic = CurrencyAdapter::instance().getCurrency().calculateStakeDifficulty(baseDiff, base_stake, m_stake_amount);
  }

  void Miner::stakeMixinChanged(int _mixin) {
    m_stake_mixin = _mixin;
    qDebug() << "Stake mixin changed to " << m_stake_mixin;
  }

}