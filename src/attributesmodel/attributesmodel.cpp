#include "attributesmodel.h"

#include "../theapp.h"
#include "../servertreemodel/shvnodeitem.h"

#include <shv/chainpack/cponwriter.h>
#include <shv/chainpack/rpcvalue.h>
#include <shv/core/utils.h>
#include <shv/coreqt/log.h>
#include <shv/core/assert.h>

#include <QSettings>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QIcon>

namespace cp = shv::chainpack;

AttributesModel::AttributesModel(QObject *parent)
	: Super(parent)
{
}

AttributesModel::~AttributesModel()
{
}

int AttributesModel::rowCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent)
	if(m_shvTreeNodeItem.isNull())
		return 0;
	return m_shvTreeNodeItem->methods().count();
}

Qt::ItemFlags AttributesModel::flags(const QModelIndex &ix) const
{
	Qt::ItemFlags ret = Super::flags(ix);
	/*
	bool editable = false;
	if(ix.column() == 1) {
		ValueAttributeNode *nd = dynamic_cast<ValueAttributeNode*>(itemFromIndex(ix.sibling(ix.row(), 0)));
		//shvInfo() << ix.row() << nd << m_userAccessLevel;
		if(nd) {
			//editable = (m_userAccessLevel & qfopcua::AccessLevel::CurrentWrite);
		}
	}
	if(editable)
		ret |= Qt::ItemIsEditable;
	else {
		ret &= ~Qt::ItemIsEditable;
	}
	*/
	return ret;
}

QVariant AttributesModel::data(const QModelIndex &ix, int role) const
{
	if(m_shvTreeNodeItem.isNull())
		return QVariant();
	const QVector<ShvMetaMethod> &mms = m_shvTreeNodeItem->methods();
	if(ix.row() < 0 || ix.row() >= mms.count())
		return QVariant();

	switch (role) {
	case Qt::DisplayRole: {
		switch (ix.column()) {
		case ColMethodName:
		case ColParams:
		case ColResult:
			return m_rows.value(ix.row()).value(ix.column());
		default:
			break;
		}
		/*
		if(resp.isError()) {
			mtd.result.clear();
			mtd.error = resp.error().toString();
		}
		else {
			std::ostringstream os(mtd.result);
			cp::CponWriter wr(os);
			wr << resp.result();
			mtd.error.clear();
		}
		*/
		break;
	}
	case Qt::DecorationRole: {
		if(ix.column() == ColBtRun) {
			static QIcon ico_run = QIcon(QStringLiteral(":/shvspy/images/run"));
			return ico_run;
		}
		break;
	}
	case Qt::ToolTipRole: {
		if(ix.column() == ColBtRun) {
			return tr("Call remote method");
		}
		break;
	}
	default:
		break;
	}
	return QVariant();
	/*
	AttributeNodeBase *nd = dynamic_cast<AttributeNodeBase*>(itemFromIndex(ix.sibling(ix.row(), 0)));
	SHV_ASSERT(nd != nullptr, QString("Internal error ix(%1, %2) %3").arg(ix.row()).arg(ix.column()).arg(ix.internalId()), return QVariant());
	if(ix.column() == 0) {
		if(role == Qt::DisplayRole)
			ret = nd->name();
		else
			ret = Super::data(ix, role);
	}
	else if(ix.column() == 1) {
		if(role == Qt::DisplayRole)
			ret = nd->displayValue();
		else if(role == Qt::ToolTipRole)
			ret = nd->displayValue();
		else if(role == Qt::EditRole)
			ret = nd->toEditorValue(nd->value());
		else
			ret = Super::data(ix, role);
	}
	*/
}
#if 0
bool AttributesModel::setData(const QModelIndex &ix, const QVariant &val, int role)
{
	shvLogFuncFrame() << val.toString() << val.typeName() << "role:" << role;
	bool ret = false;
	if(role == Qt::EditRole) {
		ValueAttributeNode *nd = dynamic_cast<ValueAttributeNode*>(itemFromIndex(ix.sibling(ix.row(), 0)));
		if(nd) {
			QVariant val_to_set = val;
			AttributeNode *pnd = dynamic_cast<AttributeNode*>(nd->parent());
			ValueAttributeNode *vnd = dynamic_cast<ValueAttributeNode*>(nd->parent());
			if(vnd) {
				// set array value
				QVariantList arr;
				for (int i = 0; i < vnd->rowCount(); ++i) {
					ValueAttributeNode *chnd = dynamic_cast<ValueAttributeNode*>(vnd->child(i));
					SHV_ASSERT(chnd != nullptr, "Bad child.", return false);
					if(i == ix.row())
						arr << chnd->fromEditorValue(val);
					else
						arr << chnd->value();
				}
				val_to_set = arr;
				pnd = dynamic_cast<AttributeNode*>(vnd->parent());
			}
			else {
				val_to_set = nd->fromEditorValue(val_to_set);
			}
			/*
			SHV_ASSERT(pnd != nullptr, "Bad parent, should be type of AttributeNode.", return false);
			qfopcua::NodeId ndid = pnd->attributesModel()->nodeId();
			int type = pnd->value().userType();
			shvInfo() << ndid.toString() << "retyping" << val_to_set << "to node type:" << QMetaType::typeName(pnd->value().userType());
			val_to_set.convert(type);
			shvInfo() << "retyped" << val_to_set;
			ret = m_client->setAttribute(ndid, pnd->attributeId(), val_to_set);
			if(ret) {
				pnd->load(true);
			}
			else {
				qfError() << "Set attribute error:" << m_client->errorString();
			}
			*/
		}
	}
	return ret;
}
#endif
QVariant AttributesModel::headerData(int section, Qt::Orientation o, int role) const
{
	QVariant ret;
	if(o == Qt::Horizontal) {
		if(role == Qt::DisplayRole) {
			if(section == ColMethodName)
				ret = tr("Method");
			else if(section == ColParams)
				ret = tr("Params");
			else if(section == ColResult)
				ret = tr("Result");
		}
	}
	return ret;
}

void AttributesModel::load(ShvNodeItem *nd)
{
	m_rows.clear();
	if(!m_shvTreeNodeItem.isNull())
		m_shvTreeNodeItem->disconnect(this);
	m_shvTreeNodeItem = nd;
	if(nd) {
		connect(nd, &ShvNodeItem::methodsLoaded, this, &AttributesModel::onMethodsLoaded, Qt::UniqueConnection);
		connect(nd, &ShvNodeItem::rpcMethodCallFinished, this, &AttributesModel::onRpcMethodCallFinished, Qt::UniqueConnection);
		nd->checkMethodsLoaded();
	}
	loadRows();
}

void AttributesModel::callMethod(int method_ix)
{
	if(m_shvTreeNodeItem.isNull())
		return;
	m_shvTreeNodeItem->callMethod(method_ix);
}

void AttributesModel::onMethodsLoaded()
{
	loadRows();
}

void AttributesModel::onRpcMethodCallFinished(int method_ix)
{
	loadRow(method_ix);
	QModelIndex ix1 = index(method_ix, 0);
	QModelIndex ix2 = index(method_ix, ColCnt - 1);
	emit dataChanged(ix1, ix2);
}

void AttributesModel::loadRow(int method_ix)
{
	if(method_ix < 0 || method_ix >= m_rows.count() || m_shvTreeNodeItem.isNull())
		return;
	const QVector<ShvMetaMethod> &mm = m_shvTreeNodeItem->methods();
	const ShvMetaMethod & mtd = mm[method_ix];
	RowVals &rv = m_rows[method_ix];
	rv[ColMethodName] = QString::fromStdString(mtd.method);
	if(mtd.params.isValid()) {
		std::ostringstream os;
		cp::CponWriter wr(os);
		wr << mtd.params;
		rv[ColParams] = QString::fromStdString(os.str());
	}
	if(mtd.response.isError()) {
		rv[ColResult] = QString::fromStdString(mtd.response.error().toString());
	}
	else if(mtd.response.result().isValid()) {
		std::ostringstream os;
		cp::CponWriter wr(os);
		wr << mtd.response.result();
		rv[ColResult] = QString::fromStdString(os.str());
	}
}

void AttributesModel::loadRows()
{
	m_rows.clear();
	if(!m_shvTreeNodeItem.isNull()) {
		const QVector<ShvMetaMethod> &mm = m_shvTreeNodeItem->methods();
		for (int i = 0; i < mm.count(); ++i) {
			const ShvMetaMethod & mtd = mm[i];
			RowVals rv;
			rv.resize(ColCnt);
			m_rows.insert(m_rows.count(), rv);
			loadRow(m_rows.count() - 1);
			rv[ColMethodName] = QString::fromStdString(mtd.method);
			{
				std::ostringstream os;
				cp::CponWriter wr(os);
				wr << mtd.params;
				rv[ColParams] = QString::fromStdString(os.str());
			}
			if(mtd.response.isError()) {
				rv[ColResult] = QString::fromStdString(mtd.response.error().toString());
			}
			else {
				std::ostringstream os;
				cp::CponWriter wr(os);
				wr << mtd.response.result();
				rv[ColResult] = QString::fromStdString(os.str());
			}
		}
	}
	emit layoutChanged();
}
/*
void AttributesModel::onRpcMessageReceived(const shv::chainpack::RpcMessage &msg)
{
	if(msg.isResponse()) {
		cp::RpcResponse resp(msg);
		if(resp.requestId() == m_rpcRqId) {
			for(const cp::RpcValue &val : resp.result().toList()) {
				appendRow(QList<QStandardItem*>{
							  new QStandardItem(QString::fromStdString(val.toString())),
							  new QStandardItem("<not called>"),
						  });
			}
		}
	}
}
*/
