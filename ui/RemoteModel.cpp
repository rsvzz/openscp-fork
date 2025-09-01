#include "RemoteModel.hpp"
#include <QVariant>
#include <QDateTime>
#include <QLocale>

RemoteModel::RemoteModel(openscp::SftpClient* client, QObject* parent)
  : QAbstractListModel(parent), client_(client) {}

int RemoteModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(items_.size());
}

QVariant RemoteModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= (int)items_.size())
    return {};
  const auto& it = items_[index.row()];
  if (role == Qt::DisplayRole) {
    return it.name + (it.isDir ? "/" : "");
  }
  if (role == Qt::ToolTipRole) {
    const auto &it = items_[index.row()];
    if (it.isDir) return "Carpeta";
    QString tip = "Archivo";
    if (it.size >= 0) tip += QString(" • %1 bytes").arg(it.size);
    if (it.mtime > 0) {
      QDateTime dt = QDateTime::fromSecsSinceEpoch((qint64)it.mtime);
      tip += " • " + QLocale().toString(dt, QLocale::ShortFormat);
    }
    return tip;
  }
  return {};
}

Qt::ItemFlags RemoteModel::flags(const QModelIndex& index) const {
  if (!index.isValid()) return Qt::NoItemFlags;
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool RemoteModel::setRootPath(const QString& path, QString* errorOut) {
  if (!client_) {
    if (errorOut) *errorOut = "Sin cliente SFTP";
    return false;
  }
  std::vector<openscp::FileInfo> out;
  std::string err;
  if (!client_->list(path.toStdString(), out, err)) {
    if (errorOut) *errorOut = QString::fromStdString(err);
    return false;
  }

  beginResetModel();
  items_.clear();
  items_.reserve(out.size());
  for (const auto& f : out) {
    items_.push_back({ QString::fromStdString(f.name), f.is_dir, f.size, f.mtime });
  }
  currentPath_ = path;
  endResetModel();
  return true;
}

bool RemoteModel::isDir(const QModelIndex& idx) const {
  if (!idx.isValid()) return false;
  return items_[idx.row()].isDir;
}
QString RemoteModel::nameAt(const QModelIndex& idx) const {
  if (!idx.isValid()) return {};
  return items_[idx.row()].name;
}
