#ifndef ORACLEIMPORTER_H
#define ORACLEIMPORTER_H

#include <carddatabase.h>
#include <QHttp>

class QBuffer;
class QXmlStreamReader;

class SetToDownload {
private:
	QString shortName, longName, url;
	bool import;
public:
	const QString &getShortName() const { return shortName; }
	const QString &getLongName() const { return longName; }
	const QString &getUrl() const { return url; }
	bool getImport() const { return import; }
	void setImport(bool _import) { import = _import; }
	SetToDownload(const QString &_shortName, const QString &_longName, const QString &_url, bool _import)
		: shortName(_shortName), longName(_longName), url(_url), import(_import) { }
};

class OracleImporter : public CardDatabase {
	Q_OBJECT
private:
	QList<SetToDownload> allSets, setsToDownload;
        QString pictureUrl, pictureUrlHq, pictureUrlStripped, setUrl;
	QString dataDir;
	int setIndex;
	int reqId;
	QBuffer *buffer;
	QHttp *http;
        QString getURLFromNameAndCardSet(QString name, QString set, bool hq, QString baseUrl) const;
	
	void downloadNextFile();
	void readSetsFromXml(QXmlStreamReader &xml);
        CardInfo *addCard(QString cardName, const QString &cardCost, const QString &cardType, const QString &cardPT, const QStringList &cardText, const QString setShortName);
private slots:
	void httpRequestFinished(int requestId, bool error);
	void readResponseHeader(const QHttpResponseHeader &responseHeader);
signals:
	void setIndexChanged(int cardsImported, int setIndex, const QString &nextSetName);
	void dataReadProgress(int bytesRead, int totalBytes);
public:
	OracleImporter(const QString &_dataDir, QObject *parent = 0);
	void readSetsFromByteArray(const QByteArray &data);
	void readSetsFromFile(const QString &fileName);
	int startDownload();
	int importTextSpoiler(CardSet *set, const QByteArray &data);
	QList<SetToDownload> &getSets() { return allSets; }
	const QString &getDataDir() const { return dataDir; }
};

#endif
