#include "shvbrokernodeitem.h"
#include "../theapp.h"
#include "../appclioptions.h"
#include "../log/rpcnotificationsmodel.h"
#include "../attributesmodel/attributesmodel.h"

#include <shv/iotqt/rpc/clientconnection.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/rpc/deviceappclioptions.h>
#include <shv/iotqt/rpc/rpcresponsecallback.h>
#include <shv/iotqt/node/shvnode.h>
#include <shv/core/utils/shvpath.h>
#include <shv/visu/errorlogmodel.h>

#include <shv/chainpack/rpcmessage.h>
#include <shv/core/stringview.h>
#include <shv/core/utils.h>
#include <shv/coreqt/log.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>

namespace cp = shv::chainpack;

const QString ShvBrokerNodeItem::SUBSCRIPTIONS = QStringLiteral("subscriptions");

struct ShvBrokerNodeItem::RpcRequestInfo
{
	std::string shvPath;
	QElapsedTimer startTS;

	RpcRequestInfo()
	{
		startTS.start();
	}
};

ShvBrokerNodeItem::ShvBrokerNodeItem(ServerTreeModel *m, const std::string &server_name)
	: Super(m, server_name)
{
	static int s_broker_id = 0;
	m_brokerId = ++ s_broker_id;

	QTimer *rpc_rq_timeout = new QTimer(this);
	rpc_rq_timeout->start(5000);
	connect(rpc_rq_timeout, &QTimer::timeout, [this]() {
		QElapsedTimer tm2;
		tm2.start();
		auto it = m_runningRpcRequests.begin();
		while (it != m_runningRpcRequests.end()) {
			if(it->second.startTS.msecsTo(tm2) > shv::iotqt::rpc::ClientConnection::defaultRpcTimeoutMsec()) {
				shvWarning() << "RPC request timeout expired for node:" << it->second.shvPath;
				it = m_runningRpcRequests.erase(it);
			}
			else
				++it;
		}
	});
}

ShvBrokerNodeItem::~ShvBrokerNodeItem()
{
	if(m_rpcConnection) {
		disconnect(m_rpcConnection, nullptr, this, nullptr);
		delete m_rpcConnection;
	}
}

QVariant ShvBrokerNodeItem::data(int role) const
{
	QVariant ret;
	if(role == Qt::DisplayRole) {
		ret = QString::fromStdString(nodeId());
		//if(m_clientConnection) {
		//	ret = m_clientConnection->serverName();
		//}
	}
	else if(role == Qt::DecorationRole) {
		static QIcon ico_connected = QIcon(QStringLiteral(":/shvspy/images/connected.png"));
		static QIcon ico_connecting = QIcon(QStringLiteral(":/shvspy/images/connecting.png"));
		static QIcon ico_disconnected = QIcon(QStringLiteral(":/shvspy/images/disconnected.png"));
		switch (openStatus()) {
		case OpenStatus::Connected: return ico_connected;
		case OpenStatus::Connecting: return ico_connecting;
		case OpenStatus::Disconnected: return ico_disconnected;
		default: return QIcon();
		}
	}
	else
		ret = Super::data(role);
	return ret;
}

QVariantMap ShvBrokerNodeItem::serverProperties() const
{
	return m_serverPropeties;
}

void ShvBrokerNodeItem::setSubscriptionList(const QVariantList &subs)
{
	m_serverPropeties[SUBSCRIPTIONS] = subs;
}


void ShvBrokerNodeItem::addSubscription(const std::string &shv_path, const std::string &method)
{
	int rqid = callSubscribe(shv_path, method);

	shv::iotqt::rpc::RpcResponseCallBack *cb = new shv::iotqt::rpc::RpcResponseCallBack(m_rpcConnection, rqid, this);
	cb->start(5000, this, [this, shv_path, method](const cp::RpcResponse &resp) {
		if(resp.isError() || (resp.result() == false)){
			emit subscriptionAddError(shv_path, resp.error().message());
		}
		else{
			emit subscriptionAdded(shv_path, method);
		}
	});
}

void ShvBrokerNodeItem::enableSubscription(const std::string &shv_path, const std::string &method, bool is_enabled)
{
	if (is_enabled){
		callSubscribe(shv_path, method);
	}
	else{
		callUnsubscribe(shv_path, method);
	}
}

void ShvBrokerNodeItem::setServerProperties(const QVariantMap &props)
{
	if(m_rpcConnection) {
		delete m_rpcConnection;
		m_rpcConnection = nullptr;
	}
	m_serverPropeties = props;
	setNodeId(m_serverPropeties.value("name").toString().toStdString());
	m_shvRoot = m_serverPropeties.value("shvRoot").toString().toStdString();
}

const std::string& ShvBrokerNodeItem::shvRoot() const
{
	return m_shvRoot;
}

void ShvBrokerNodeItem::open()
{
	close();
	shv::iotqt::rpc::ClientConnection *cli = clientConnection();
	//cli->setServerName(props.value("name").toString());
	cli->setHost(m_serverPropeties.value("host").toString().toStdString());
	cli->setPort(m_serverPropeties.value("port").toInt());
	cli->setSecurityType(m_serverPropeties.value("securityType").toString().toStdString());
	cli->setPeerVerify(m_serverPropeties.value("peerVerify").toBool());
	cli->setUser(m_serverPropeties.value("user").toString().toStdString());
	std::string pwd = m_serverPropeties.value("password").toString().toStdString();
	cli->setPassword(pwd);
	cli->setLoginType(pwd.size() == 40? cp::IRpcConnection::LoginType::Sha1: cp::IRpcConnection::LoginType::Plain);
	cli->open();
	m_openStatus = OpenStatus::Connecting;
	emitDataChanged();
}

void ShvBrokerNodeItem::close()
{
	//if(openStatus() == OpenStatus::Disconnected)
	//	return;
	if(m_rpcConnection)
		m_rpcConnection->close();
	m_openStatus = OpenStatus::Disconnected;
	deleteChildren();
	emitDataChanged();
}

shv::iotqt::rpc::ClientConnection *ShvBrokerNodeItem::clientConnection()
{
	if(!m_rpcConnection) {
		QString conn_type = m_serverPropeties.value("connectionType").toString();

		shv::iotqt::rpc::DeviceAppCliOptions opts;
		{
			int proto_type = m_serverPropeties.value("rpc.protocolType").toInt();
			if(proto_type == (int)cp::Rpc::ProtocolType::JsonRpc)
				opts.setProtocolType("jsonrpc");
			else if(proto_type == (int)cp::Rpc::ProtocolType::Cpon)
				opts.setProtocolType("cpon");
			else
				opts.setProtocolType("chainpack");
		}
		{
			QVariant v = m_serverPropeties.value("rpc.reconnectInterval");
			if(v.isValid())
				opts.setReconnectInterval(v.toInt());
		}
		{
			QVariant v = m_serverPropeties.value("rpc.heartbeatInterval");
			if(v.isValid())
				opts.setHeartbeatInterval(v.toInt());
		}
		{
			QVariant v = m_serverPropeties.value("rpc.defaultRpcTimeout");
			if(v.isValid())
				opts.setDefaultRpcTimeout(v.toInt());
		}
		{
			QString dev_id = m_serverPropeties.value("device.id").toString();
			if(!dev_id.isEmpty())
				opts.setDeviceId(dev_id.toStdString());
		}
		{
			QString mount_point = m_serverPropeties.value("device.mountPoint").toString();
			if(!mount_point.isEmpty())
				opts.setMountPoint(mount_point.toStdString());
		}
		if(conn_type == "device") {
			auto *c = new shv::iotqt::rpc::DeviceConnection(nullptr);
			c->setCliOptions(&opts);
			m_rpcConnection = c;
		}
		else {
			m_rpcConnection = new shv::iotqt::rpc::ClientConnection(nullptr);
			m_rpcConnection->setCliOptions(&opts);
		}
		//m_rpcConnection->setCheckBrokerConnectedInterval(0);
		connect(m_rpcConnection, &shv::iotqt::rpc::ClientConnection::brokerConnectedChanged, this, &ShvBrokerNodeItem::onBrokerConnectedChanged);
		connect(m_rpcConnection, &shv::iotqt::rpc::ClientConnection::rpcMessageReceived, this, &ShvBrokerNodeItem::onRpcMessageReceived);
	}
	return m_rpcConnection;
}

void ShvBrokerNodeItem::onBrokerConnectedChanged(bool is_connected)
{
	m_openStatus = is_connected? OpenStatus::Connected: OpenStatus::Disconnected;
	emitDataChanged();
	if(is_connected) {
		createSubscriptions();
		loadChildren();
		AttributesModel *m = TheApp::instance()->attributesModel();
		m->load(this);
	}
	else {
		close();
	}

	emit brokerConnectedChange(is_connected);
}

ShvNodeItem* ShvBrokerNodeItem::findNode(const std::string &path_, std::string *path_rest)
{
	shvLogFuncFrame() << path_ << "shv root:" << shvRoot();
	ShvNodeItem *ret = this;
	std::string path = path_;
	if(!shvRoot().empty()) {
		path = path.substr(shvRoot().size());
		if(path.size() && path[0] == '/')
			path = path.substr(1);
	}
	shv::core::StringViewList id_list = shv::core::utils::ShvPath::split(path);

	for(const shv::core::StringView &node_id : id_list) {
		int i;
		int row_cnt = ret->childCount();
		for (i = 0; i < row_cnt; ++i) {
			ShvNodeItem *nd = ret->childAt(i);
			if(nd) {
				if(node_id == nd->nodeId()) {
					ret = nd;
					break;
				}
			}
		}
		if(i == row_cnt) {
			if(path_rest)
				*path_rest = path.substr(node_id.start());
			return nullptr;
		}
	}
	return ret;
}

int ShvBrokerNodeItem::callSubscribe(const std::string &shv_path, std::string method)
{
	shv::iotqt::rpc::ClientConnection *cc = clientConnection();
	int rqid = cc->callMethodSubscribe(shv_path, method);
	return rqid;
}

int ShvBrokerNodeItem::callUnsubscribe(const std::string &shv_path, std::string method)
{
	shv::iotqt::rpc::ClientConnection *cc = clientConnection();
	int rqid = cc->callMethodUnsubscribe(shv_path, method);
	return rqid;
}

int ShvBrokerNodeItem::callNodeRpcMethod(const std::string &calling_node_shv_path, const std::string &method, const cp::RpcValue &params)
{
	shvLogFuncFrame() << calling_node_shv_path;
	shv::iotqt::rpc::ClientConnection *cc = clientConnection();
	int rqid = cc->callShvMethod(calling_node_shv_path, method, params);
	m_runningRpcRequests[rqid].shvPath = calling_node_shv_path;
	return rqid;
}

void ShvBrokerNodeItem::onRpcMessageReceived(const shv::chainpack::RpcMessage &msg)
{
	if(msg.isResponse()) {
		cp::RpcResponse resp(msg);
		if(resp.isError())
			TheApp::instance()->errorLogModel()->addLogRow(
						NecroLog::Level::Error
						, resp.error().message()
						, QString::fromStdString(cp::RpcResponse::Error::errorCodeToString(resp.error().code()))
						);
		int rqid = resp.requestId().toInt();
		auto it = m_runningRpcRequests.find(rqid);
		if(it == m_runningRpcRequests.end()) {
			//shvWarning() << "unexpected request id:" << rqid;
			// can be load attributes request
			return;
		}
		const std::string &path = it->second.shvPath;
		ShvNodeItem *nd = findNode(path);
		if(nd) {
			nd->processRpcMessage(msg);
		}
		else {
			shvError() << "Running RPC request response arrived - cannot find node on path:" << path;
		}
		m_runningRpcRequests.erase(it);
	}
	else if(msg.isRequest()) {
		cp::RpcRequest rq(msg);
		cp::RpcResponse resp = cp::RpcResponse::forRequest(rq);
		try {
			//shvInfo() << "RPC request received:" << rq.toCpon();
			cp::RpcValue shv_path = rq.shvPath();
			if(!shv_path.toString().empty())
				SHV_EXCEPTION("Invalid path: " + shv_path.toString());
			const cp::RpcValue method = rq.method();
			if(method == cp::Rpc::METH_DIR) {
				resp.setResult(cp::RpcValue::List{
								   cp::Rpc::METH_DIR,
								   //cp::Rpc::METH_PING,
								   cp::Rpc::METH_APP_NAME,
								   //cp::Rpc::METH_CONNECTION_TYPE,
							   });
			}
			//else if(method.toString() == cp::Rpc::METH_PING) {
			//	resp.setResult(true);
			//}
			else if(method.toString() == cp::Rpc::METH_APP_NAME) {
				resp.setResult(QCoreApplication::instance()->applicationName().toStdString());
			}
			//else if(method.toString() == cp::Rpc::METH_CONNECTION_TYPE) {
			//	resp.setResult(m_rpcConnection->connectionType());
			//}
		}
		catch (shv::core::Exception &e) {
			resp.setError(cp::RpcResponse::Error::create(cp::RpcResponse::Error::MethodCallException, e.message()));
		}
		m_rpcConnection->sendMessage(resp);
	}
	else if(msg.isSignal()) {
		shvDebug() << msg.toCpon();
		if(serverProperties().value(QStringLiteral("muteHeartBeats")).toBool()) {
			if(msg.method().toString() == "appserver.heartBeat")
				return;
		}
		RpcNotificationsModel *m = TheApp::instance()->rpcNotificationsModel();
		m->addLogRow(nodeId(), msg);
	}
}

void ShvBrokerNodeItem::createSubscriptions()
{
	QMetaEnum meta_sub = QMetaEnum::fromType<SubscriptionItem>();
	QVariant v = m_serverPropeties.value(SUBSCRIPTIONS);
	if(v.isValid()) {
		QVariantList subs = v.toList();

		for (int i = 0; i < subs.size(); i++) {
			QVariantMap s = subs.at(i).toMap();

			if (s.value(meta_sub.valueToKey(SubscriptionItem::IsEnabled)).toBool()){
				callSubscribe(s.value(meta_sub.valueToKey(SubscriptionItem::Path)).toString().toStdString(), s.value(meta_sub.valueToKey(SubscriptionItem::Method)).toString().toStdString());
			}
		}
	}
}

