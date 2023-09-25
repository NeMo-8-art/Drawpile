// SPDX-License-Identifier: GPL-3.0-or-later
#include "libclient/net/client.h"
#include "libclient/net/login.h"
#include "libclient/net/message.h"
#include "libclient/net/tcpserver.h"
#include "libshared/net/servercmd.h"
#include "libshared/util/qtcompat.h"
#include <QDebug>
#ifdef Q_OS_ANDROID
#	include "libshared/util/androidutils.h"
#endif

namespace net {

Client::Client(QObject *parent)
	: QObject(parent)
{
}

void Client::connectToServer(
	int timeoutSecs, LoginHandler *loginhandler, bool builtin)
{
	Q_ASSERT(!isConnected());
	m_builtin = builtin;

	TcpServer *server = new TcpServer(timeoutSecs, this);
	m_server = server;
	m_server->setSmoothDrainRate(m_smoothDrainRate);

#ifdef Q_OS_ANDROID
	if(!m_wakeLock) {
		QString tag{QStringLiteral("Drawpile::TcpWake%1")
						.arg(
							reinterpret_cast<quintptr>(server),
							QT_POINTER_SIZE * 2, 16, QLatin1Char('0'))};
		m_wakeLock = new utils::AndroidWakeLock{"PARTIAL_WAKE_LOCK", tag};
	}

	if(!m_wifiLock) {
		QString tag{QStringLiteral("Drawpile::TcpWifi%1")
						.arg(
							reinterpret_cast<quintptr>(server),
							QT_POINTER_SIZE * 2, 16, QLatin1Char('0'))};
		m_wifiLock =
			new utils::AndroidWifiLock{"WIFI_MODE_FULL_LOW_LATENCY", tag};
	}
#endif

	connect(server, &TcpServer::loggingOut, this, &Client::serverDisconnecting);
	connect(
		server, &TcpServer::serverDisconnected, this,
		&Client::handleDisconnect);
	connect(
		server, &TcpServer::serverDisconnected, loginhandler,
		&LoginHandler::serverDisconnected);
	connect(server, &TcpServer::loggedIn, this, &Client::handleConnect);
	connect(
		server, &TcpServer::messagesReceived, this, &Client::handleMessages);

	connect(server, &TcpServer::bytesReceived, this, &Client::bytesReceived);
	connect(server, &TcpServer::bytesSent, this, &Client::bytesSent);
	connect(server, &TcpServer::lagMeasured, this, &Client::lagMeasured);

	connect(
		server, &TcpServer::gracefullyDisconnecting, this,
		[this](
			MessageQueue::GracefulDisconnect reason, const QString &message) {
			if(reason == MessageQueue::GracefulDisconnect::Kick) {
				emit youWereKicked(message);
				return;
			}

			QString chat;
			switch(reason) {
			case MessageQueue::GracefulDisconnect::Kick:
				emit youWereKicked(message);
				return;
			case MessageQueue::GracefulDisconnect::Error:
				chat = tr("A server error occurred!");
				break;
			case MessageQueue::GracefulDisconnect::Shutdown:
				chat = tr("The server is shutting down!");
				break;
			default:
				chat = "Unknown error";
			}

			if(!message.isEmpty())
				chat = QString("%1 (%2)").arg(chat, message);

			emit serverMessage(chat, true);
		});

	if(loginhandler->mode() == LoginHandler::Mode::HostRemote)
		loginhandler->setUserId(m_myId);

	emit serverConnected(
		loginhandler->url().host(), loginhandler->url().port());
	server->login(loginhandler);

	m_catchupTo = 0;
	m_caughtUp = 0;
	m_catchupProgress = 0;
}

void Client::disconnectFromServer()
{
	if(m_server)
		m_server->logout();
}

QUrl Client::sessionUrl(bool includeUser) const
{
	QUrl url = m_lastUrl;
	if(!includeUser)
		url.setUserInfo(QString());
	return url;
}

void Client::handleConnect(
	const QUrl &url, uint8_t userid, bool join, bool auth, bool moderator,
	bool supportsAutoReset, bool compatibilityMode, const QString &joinPassword)
{
	m_lastUrl = url;
	m_myId = userid;
	m_moderator = moderator;
	m_isAuthenticated = auth;
	m_supportsAutoReset = supportsAutoReset;
	m_compatibilityMode = compatibilityMode;

	emit serverLoggedIn(join, m_compatibilityMode, joinPassword);
}

void Client::handleDisconnect(
	const QString &message, const QString &errorcode, bool localDisconnect)
{
	Q_ASSERT(isConnected());

	m_compatibilityMode = false;
	emit serverDisconnected(message, errorcode, localDisconnect);
	m_server->deleteLater();
	m_server = nullptr;
	m_moderator = false;

#ifdef Q_OS_ANDROID
	delete m_wakeLock;
	delete m_wifiLock;
	m_wakeLock = nullptr;
	m_wifiLock = nullptr;
#endif
}

int Client::uploadQueueBytes() const
{
	if(m_server)
		return m_server->uploadQueueBytes();
	return 0;
}


void Client::sendMessage(const net::Message &msg)
{
	sendMessages(1, &msg);
}

void Client::sendMessages(int count, const net::Message *msgs)
{
	if(m_compatibilityMode) {
		QVector<net::Message> compatibleMsgs =
			filterCompatibleMessages(count, msgs);
		sendCompatibleMessages(
			compatibleMsgs.count(), compatibleMsgs.constData());
	} else {
		sendCompatibleMessages(count, msgs);
	}
}

void Client::sendCompatibleMessages(int count, const net::Message *msgs)
{
	if(count > 0) {
		emit drawingCommandsLocal(count, msgs);
		// Note: we could emit drawingCommandLocal only in connected mode,
		// but it's good to exercise the code path in local mode too
		// to make potential bugs more obvious.
		if(m_server) {
			m_server->sendMessages(count, msgs);
		} else {
			emit messagesReceived(count, msgs);
		}
	}
}

void Client::sendResetMessage(const net::Message &msg)
{
	sendResetMessages(1, &msg);
}

void Client::sendResetMessages(int count, const net::Message *msgs)
{
	if(m_compatibilityMode) {
		QVector<net::Message> compatibleMsgs =
			filterCompatibleMessages(count, msgs);
		sendCompatibleResetMessages(
			compatibleMsgs.count(), compatibleMsgs.constData());
	} else {
		sendCompatibleResetMessages(count, msgs);
	}
}

void Client::sendCompatibleResetMessages(int count, const net::Message *msgs)
{
	if(count > 0) {
		if(m_server) {
			m_server->sendMessages(count, msgs);
		} else {
			emit messagesReceived(count, msgs);
		}
	}
}

QVector<net::Message>
Client::filterCompatibleMessages(int count, const net::Message *msgs)
{
	// Ideally, the client shouldn't be attempting to send any incompatible
	// messages in the first place, but we'll err on the side of caution. In
	// particular, a thick server will kick us out if we send a wrong message.
	QVector<net::Message> compatibleMsgs;
	compatibleMsgs.reserve(count);
	for(int i = 0; i < count; ++i) {
		const net::Message &msg = msgs[i];
		const net::Message compatibleMsg =
			net::makeMessageBackwardCompatible(msg);
		if(compatibleMsg.isNull()) {
			qWarning("Incompatible %s message", qUtf8Printable(msg.typeName()));
		} else {
			compatibleMsgs.append(compatibleMsg);
		}
	}
	return compatibleMsgs;
}

void Client::handleMessages(int count, net::Message *msgs)
{
	for(int i = 0; i < count; ++i) {
		net::Message &msg = msgs[i];
		switch(msg.type()) {
		case DP_MSG_SERVER_COMMAND:
			handleServerReply(ServerReply::fromMessage(msg));
			break;
		case DP_MSG_DATA:
			handleData(msg);
			break;
		case DP_MSG_DRAW_DABS_CLASSIC:
		case DP_MSG_DRAW_DABS_PIXEL:
		case DP_MSG_DRAW_DABS_PIXEL_SQUARE:
			if(m_compatibilityMode) {
				msg.setIndirectCompatFlag();
			}
			break;
		default:
			break;
		}
	}
	emit messagesReceived(count, msgs);

	// The server can send a "catchup" message when there is a significant
	// number of messages queued. During login, we can show a progress bar and
	// hide the canvas to speed up the initial catchup phase.
	if(m_catchupTo > 0) {
		m_caughtUp += count;
		if(m_caughtUp >= m_catchupTo) {
			qInfo("Catchup: caught up to %d messages", m_caughtUp);
			emit catchupProgress(100);
			m_catchupTo = 0;
			m_server->setSmoothEnabled(true);
		} else {
			int progress = 100 * m_caughtUp / m_catchupTo;
			if(progress != m_catchupProgress) {
				m_catchupProgress = progress;
				emit catchupProgress(progress);
			}
		}
	}
}

void Client::handleServerReply(const ServerReply &msg)
{
	switch(msg.type) {
	case ServerReply::ReplyType::Unknown:
		qWarning() << "Unknown server reply:" << msg.message << msg.reply;
		break;
	case ServerReply::ReplyType::Login:
		qWarning("got login message while in session!");
		break;
	case ServerReply::ReplyType::Message:
	case ServerReply::ReplyType::Alert:
	case ServerReply::ReplyType::Error:
	case ServerReply::ReplyType::Result:
		emit serverMessage(
			translateMessage(msg.reply),
			msg.type == ServerReply::ReplyType::Alert);
		break;
	case ServerReply::ReplyType::Log: {
		QString time = QDateTime::fromString(
						   msg.reply["timestamp"].toString(), Qt::ISODate)
						   .toLocalTime()
						   .toString(Qt::ISODate);
		QString user = msg.reply["user"].toString();
		QString message = msg.message;
		if(user.isEmpty())
			emit serverLog(QStringLiteral("[%1] %2").arg(time, message));
		else
			emit serverLog(
				QStringLiteral("[%1] %2: %3").arg(time, user, message));
	} break;
	case ServerReply::ReplyType::SessionConf:
		emit sessionConfChange(msg.reply["config"].toObject());
		break;
	case ServerReply::ReplyType::SizeLimitWarning:
		// No longer used since 2.1.0. Replaced by RESETREQUEST
		break;
	case ServerReply::ReplyType::ResetRequest:
		emit autoresetRequested(
			msg.reply["maxSize"].toInt(), msg.reply["query"].toBool());
		break;
	case ServerReply::ReplyType::Status:
		emit serverStatusUpdate(msg.reply["size"].toInt());
		break;
	case ServerReply::ReplyType::Reset:
		handleResetRequest(msg);
		break;
	case ServerReply::ReplyType::Catchup:
		m_server->setSmoothEnabled(false);
		m_catchupTo = msg.reply["count"].toInt();
		qInfo("Catching up to %d messages", m_catchupTo);
		m_caughtUp = 0;
		m_catchupProgress = 0;
		emit catchupProgress(m_catchupTo > 0 ? 0 : 100);
		break;
	}
}

QString Client::translateMessage(const QJsonObject &reply)
{
	QJsonValue keyValue = reply[QStringLiteral("T")];
	if(keyValue.isString()) {
		QString key = keyValue.toString();
		QJsonObject params = reply[QStringLiteral("P")].toObject();
		if(key == net::ServerReply::KEY_BAN) {
			return tr("%1 banned by %2.")
				.arg(
					params[QStringLiteral("target")].toString(),
					params[QStringLiteral("by")].toString());
		} else if(key == net::ServerReply::KEY_KICK) {
			return tr("%1 kicked by %2.")
				.arg(
					params[QStringLiteral("target")].toString(),
					params[QStringLiteral("by")].toString());
		} else if(key == net::ServerReply::KEY_OP_GIVE) {
			QString target = params[QStringLiteral("target")].toString();
			QString by = params[QStringLiteral("by")].toString();
			if(by.isEmpty()) {
				return tr("%1 made operator by the server.").arg(target);
			} else {
				return tr("%1 made operator by %2.").arg(target, by);
			}
		} else if(key == net::ServerReply::KEY_OP_TAKE) {
			QString target = params[QStringLiteral("target")].toString();
			QString by = params[QStringLiteral("by")].toString();
			if(by.isEmpty()) {
				return tr("Operator status revoked from %1 by the server.")
					.arg(target);
			} else {
				return tr("Operator status revoked form %1 by %2.")
					.arg(target, by);
			}
		} else if(key == net::ServerReply::KEY_RESET_CANCEL) {
			return tr("Session reset cancelled! An operator must unlock the "
					  "canvas and reset the session manually.");
		} else if(key == net::ServerReply::KEY_RESET_FAILED) {
			return tr("Session reset failed! An operator must unlock the "
					  "canvas and reset the session manually.");
		} else if(key == net::ServerReply::KEY_RESET_PREPARE) {
			return tr("Preparing for session reset! Please wait, the session "
					  "should be available again shortly…");
		} else if(key == net::ServerReply::KEY_TERMINATE_SESSION) {
			return tr("Session terminated by moderator (%1).")
				.arg(params[QStringLiteral("by")].toString());
		} else if(key == net::ServerReply::KEY_TRUST_GIVE) {
			QString target = params[QStringLiteral("target")].toString();
			QString by = params[QStringLiteral("by")].toString();
			if(by.isEmpty()) {
				return tr("%1 trusted by the server.").arg(target);
			} else {
				return tr("%1 trusted by %2.").arg(target, by);
			}
		} else if(key == net::ServerReply::KEY_TRUST_TAKE) {
			QString target = params[QStringLiteral("target")].toString();
			QString by = params[QStringLiteral("by")].toString();
			if(by.isEmpty()) {
				return tr("%1 untrusted by the server.").arg(target);
			} else {
				return tr("%1 untrusted by %2.").arg(target, by);
			}
		}
	}
	return reply[QStringLiteral("message")].toString();
}

void Client::handleResetRequest(const ServerReply &msg)
{
	if(msg.reply["state"] == "init") {
		qDebug("Requested session reset");
		emit needSnapshot();

	} else if(msg.reply["state"] == "reset") {
		qDebug("Resetting session!");
		emit sessionResetted();

	} else {
		qWarning() << "Unknown reset state:" << msg.reply["state"].toString();
		qWarning() << msg.message;
	}
}

void Client::handleData(const net::Message &msg)
{
	DP_MsgData *md = msg.toData();
	if(md && DP_msg_data_recipient(md) == m_myId) {
		int type = DP_msg_data_type(md);
		switch(type) {
		case DP_MSG_DATA_TYPE_USER_INFO:
			handleUserInfo(msg, md);
			break;
		default:
			qWarning("Unknown data message type %d", type);
			break;
		}
	}
}

void Client::Client::handleUserInfo(const net::Message &msg, DP_MsgData *md)
{
	size_t size;
	const unsigned char *bytes = DP_msg_data_body(md, &size);
	QJsonParseError err;
	QJsonDocument json = QJsonDocument::fromJson(
		QByteArray::fromRawData(
			reinterpret_cast<const char *>(bytes), compat::castSize(size)),
		&err);
	if(json.isObject()) {
		QJsonObject info = json.object();
		QString type = info["type"].toString();
		if(type == "request_user_info") {
			emit userInfoRequested(msg.contextId());
		} else if(type == "user_info") {
			emit userInfoReceived(msg.contextId(), info);
		} else {
			qWarning("Unknown user info type '%s'", qUtf8Printable(type));
		}
	} else {
		qWarning(
			"Could not parse JSON as an object: %s",
			qUtf8Printable(err.errorString()));
	}
}

void Client::setSmoothDrainRate(int smoothDrainRate)
{
	m_smoothDrainRate = smoothDrainRate;
	if(m_server) {
		m_server->setSmoothDrainRate(m_smoothDrainRate);
	}
}

}
