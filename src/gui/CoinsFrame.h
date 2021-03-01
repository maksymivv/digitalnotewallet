// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QFrame>
#include <QMenu>
#include <QWidget>
#include <QAbstractItemModel>

#include <IWalletLegacy.h>

namespace Ui {
class CoinsFrame;
}

namespace WalletGui {

class VisibleOutputsModel;

class CoinsFrame : public QFrame {
  Q_OBJECT

public:
  CoinsFrame(QWidget* _parent);
  ~CoinsFrame();

  QModelIndex m_index;

  enum TypeEnum
  {
      AllTypes,
      Spent,
      Unspent
  };

public slots:
  void onCustomContextMenu(const QPoint &point);
  void chooseType(int idx);
  void changedSearchFor(const QString &searchstring);

public Q_SLOTS:
  void copyHash();
  void showDetails();

private:
  QScopedPointer<Ui::CoinsFrame> m_ui;
  QScopedPointer<VisibleOutputsModel> m_visibleOutputsModel;
  QMenu* contextMenu;

  void currentOutputChanged(const QModelIndex& _currentIndex);

  Q_SLOT void outputDoubleClicked(const QModelIndex& _index);
  
private slots:
  void resetFilterClicked();

};

}