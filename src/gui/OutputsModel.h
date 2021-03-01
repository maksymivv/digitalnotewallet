// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2016-2021 Karbo developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QVector>
#include <QAbstractItemModel>
#include <QSortFilterProxyModel>

#include <IWalletLegacy.h>

namespace WalletGui {

class OutputsModel : public QAbstractItemModel {
  Q_OBJECT
  Q_ENUMS(Columns)
  Q_ENUMS(Roles)

public:
  enum class OutputType : uint8_t { Invalid, Key, Multisignature };

  enum Columns{
    COLUMN_STATE = 0, COLUMN_TYPE, COLUMN_OUTPUT_KEY, COLUMN_TX_HASH, COLUMN_AMOUNT, COLUMN_GLOBAL_OUTPUT_INDEX, COLUMN_OUTPUT_IN_TRANSACTION,
        COLUMN_SPENDING_BLOCK_HEIGHT, COLUMN_TIMESTAMP, COLUMN_SPENDING_TRANSACTION_HASH, COLUMN_KEY_IMAGE, COLUMN_INPUT_IN_TRANSACTION
  };

  enum Roles {
    ROLE_TYPE = Qt::UserRole, ROLE_STATE, ROLE_OUTPUT_KEY, ROLE_TX_HASH, ROLE_AMOUNT, ROLE_GLOBAL_OUTPUT_INDEX, ROLE_OUTPUT_IN_TRANSACTION,
      ROLE_SPENDING_BLOCK_HEIGHT, ROLE_TIMESTAMP, ROLE_SPENDING_TRANSACTION_HASH, ROLE_KEY_IMAGE, ROLE_INPUT_IN_TRANSACTION,
      ROLE_COLUMN, ROLE_ROW
   };

  static OutputsModel& instance();

  Qt::ItemFlags flags(const QModelIndex& _index) const Q_DECL_OVERRIDE;
  int columnCount(const QModelIndex& _parent = QModelIndex()) const Q_DECL_OVERRIDE;
  int rowCount(const QModelIndex& _parent = QModelIndex()) const Q_DECL_OVERRIDE;

  QVariant headerData(int _section, Qt::Orientation _orientation, int _role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
  QVariant data(const QModelIndex& _index, int _role = Qt::DisplayRole) const Q_DECL_OVERRIDE;
  QModelIndex index(int _row, int _column, const QModelIndex& _parent = QModelIndex()) const Q_DECL_OVERRIDE;
  QModelIndex parent(const QModelIndex& _index) const Q_DECL_OVERRIDE;

private:
  QVector<CryptoNote::TransactionOutputInformation> m_outputs;
  QVector<CryptoNote::TransactionSpentOutputInformation> m_spentOutputs;

  OutputsModel();
  ~OutputsModel();

  QVariant getDisplayRole(const QModelIndex& _index) const;
  QVariant getDecorationRole(const QModelIndex& _index) const;
  QVariant getAlignmentRole(const QModelIndex& _index) const;
  QVariant getUserRole(const QModelIndex& _index, int _role, CryptoNote::TransactionOutputInformation _output,
                      CryptoNote::TransactionSpentOutputInformation& _spentOutput, bool _isSpent) const;

  void reloadWalletTransactions();
  void appendTransaction(CryptoNote::TransactionId _id);
  void reset();

  bool isOutputSpent(CryptoNote::TransactionOutputInformation& _output,
               CryptoNote::TransactionSpentOutputInformation& _spentOutput) const;
};

}
