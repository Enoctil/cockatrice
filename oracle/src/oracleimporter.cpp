#include "oracleimporter.h"
#include <QtGui>
#include <QtNetwork>
#include <QXmlStreamReader>
#include <QDomDocument>

OracleImporter::OracleImporter(const QString &_dataDir, QObject *parent)
	: CardDatabase(parent), dataDir(_dataDir), setIndex(-1)
{
	buffer = new QBuffer(this);
	http = new QHttp(this);
	connect(http, SIGNAL(requestFinished(int, bool)), this, SLOT(httpRequestFinished(int, bool)));
	connect(http, SIGNAL(responseHeaderReceived(const QHttpResponseHeader &)), this, SLOT(readResponseHeader(const QHttpResponseHeader &)));
	connect(http, SIGNAL(dataReadProgress(int, int)), this, SIGNAL(dataReadProgress(int, int)));
}

void OracleImporter::readSetsFromFile(const QString &fileName)
{
	QFile setsFile(fileName);
	if (!setsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QMessageBox::critical(0, tr("Error"), tr("Cannot open file '%1'.").arg(fileName));
		return;
	}

	QXmlStreamReader xml(&setsFile);
	readSetsFromXml(xml);
}

void OracleImporter::readSetsFromByteArray(const QByteArray &data)
{
	QXmlStreamReader xml(data);
	readSetsFromXml(xml);
}

void OracleImporter::readSetsFromXml(QXmlStreamReader &xml)
{
	allSets.clear();
	
        QString edition;
	QString editionLong;
	QString editionURL;
	while (!xml.atEnd()) {
		if (xml.readNext() == QXmlStreamReader::EndElement)
			break;
		if (xml.name() == "set") {
			QString shortName, longName;
			bool import = xml.attributes().value("import").toString().toInt();
			while (!xml.atEnd()) {
				if (xml.readNext() == QXmlStreamReader::EndElement)
					break;
				if (xml.name() == "name")
					edition = xml.readElementText();
				else if (xml.name() == "longname")
					editionLong = xml.readElementText();
				else if (xml.name() == "url")
					editionURL = xml.readElementText();
			}
			allSets << SetToDownload(edition, editionLong, editionURL, import);
			edition = editionLong = editionURL = QString();
		} else if (xml.name() == "picture_url")
			pictureUrl = xml.readElementText();
                else if (xml.name() == "picture_url_hq")
                        pictureUrlHq = xml.readElementText();
		else if (xml.name() == "set_url")
			setUrl = xml.readElementText();
	}
}

CardInfo *OracleImporter::addCard(QString cardName, const QString &cardCost, const QString &cardType, const QString &cardPT, const QStringList &cardText, const QString setShortName)
{
	QString fullCardText = cardText.join("\n");
	bool splitCard = false;
	if (cardName.contains('(')) {
		cardName.remove(QRegExp(" \\(.*\\)"));
		splitCard = true;
	}
	
	CardInfo *card;
	if (cardHash.contains(cardName)) {
		card = cardHash.value(cardName);
		if (splitCard && !card->getText().contains(fullCardText))
			card->setText(card->getText() + "\n---\n" + fullCardText);
	} else {
		// Workaround for card name weirdness
		if (cardName.contains("XX"))
			cardName.remove("XX");
		cardName = cardName.replace("Æ", "Ae");
		
		bool mArtifact = false;
		if (cardType.endsWith("Artifact"))
			for (int i = 0; i < cardText.size(); ++i)
				if (cardText[i].contains("{T}") && cardText[i].contains("to your mana pool"))
					mArtifact = true;
					
		QStringList colors;
		QStringList allColors = QStringList() << "W" << "U" << "B" << "R" << "G";
		for (int i = 0; i < allColors.size(); i++)
			if (cardCost.contains(allColors[i]))
				colors << allColors[i];
		
		if (cardText.contains(cardName + " is white."))
			colors << "W";
		if (cardText.contains(cardName + " is blue."))
			colors << "U";
		if (cardText.contains(cardName + " is black."))
			colors << "B";
		if (cardText.contains(cardName + " is red."))
			colors << "R";
		if (cardText.contains(cardName + " is green."))
			colors << "G";
		
		bool cipt = (cardText.contains(cardName + " enters the battlefield tapped."));
		
		card = new CardInfo(this, cardName, cardCost, cardType, cardPT, fullCardText, colors, cipt);

                card->setPicURL(getURLFromNameAndCardSet(cardName, setShortName, false));
                card->setPicHqURL(getURLFromNameAndCardSet(cardName, setShortName, true));

		int tableRow = 1;
		QString mainCardType = card->getMainCardType();
		if ((mainCardType == "Land") || mArtifact)
			tableRow = 0;
		else if ((mainCardType == "Sorcery") || (mainCardType == "Instant"))
			tableRow = 3;
		else if (mainCardType == "Creature")
			tableRow = 2;
		card->setTableRow(tableRow);

		cardHash.insert(cardName, card);
	}
	return card;
}

int OracleImporter::importTextSpoiler(CardSet *set, const QByteArray &data)
{
	int cards = 0;
	QString bufferContents(data);
	
	// Workaround for ampersand bug in text spoilers
	int index = -1;
	while ((index = bufferContents.indexOf('&', index + 1)) != -1) {
		int semicolonIndex = bufferContents.indexOf(';', index);
		if (semicolonIndex > 5) {
			bufferContents.insert(index + 1, "amp;");
			index += 4;
		}
	}
	
	QDomDocument doc;
	QString errorMsg;
	int errorLine, errorColumn;
	if (!doc.setContent(bufferContents, &errorMsg, &errorLine, &errorColumn))
		qDebug(QString("error: %1, line=%2, column=%3").arg(errorMsg).arg(errorLine).arg(errorColumn).toLatin1());

	QDomNodeList divs = doc.elementsByTagName("div");
	for (int i = 0; i < divs.size(); ++i) {
		QDomElement div = divs.at(i).toElement();
		QDomNode divClass = div.attributes().namedItem("class");
		if (divClass.nodeValue() == "textspoiler") {
			QString cardName, cardCost, cardType, cardPT, cardText;
			
			QDomNodeList trs = div.elementsByTagName("tr");
			for (int j = 0; j < trs.size(); ++j) {
				QDomElement tr = trs.at(j).toElement();
				QDomNodeList tds = tr.elementsByTagName("td");
				if (tds.size() != 2) {
					QStringList cardTextSplit = cardText.split("\n");
					for (int i = 0; i < cardTextSplit.size(); ++i)
						cardTextSplit[i] = cardTextSplit[i].trimmed();
					
                                        CardInfo *card = addCard(cardName, cardCost, cardType, cardPT, cardTextSplit, set->getShortName());
					if (!set->contains(card)) {
						card->addToSet(set);
						cards++;
					}
					cardName = cardCost = cardType = cardPT = cardText = QString();
				} else {
					QString v1 = tds.at(0).toElement().text().simplified();
					QString v2 = tds.at(1).toElement().text().replace(trUtf8("—"), "-");
					
					if (v1 == "Name:")
						cardName = v2.simplified();
					else if (v1 == "Cost:")
						cardCost = v2.simplified();
					else if (v1 == "Type:")
						cardType = v2.simplified();
					else if (v1 == "Pow/Tgh:")
						cardPT = v2.simplified();
					else if (v1 == "Rules Text:")
						cardText = v2.trimmed();
				}
			}
			break;
		}
	}
	return cards;
}

QString OracleImporter::getURLFromNameAndCardSet(QString name, QString setName, bool hq) const
{
        QString simpleName = name
                             .replace("Æther", "Aether")
                             .replace("ö", "o")
                             .remove('\'')
                             .remove("//")
                             .remove(QRegExp("\\(.*\\)"))
                             .simplified();

        if (!hq) {
            simpleName = simpleName.remove(',')
                         .remove(':')
                         .remove('.')
                         .replace(' ', '_')
                         .replace('-', '_');
            return pictureUrl.arg(simpleName, setName);
        }
        else
            return pictureUrlHq.arg(simpleName, setName);

}

int OracleImporter::startDownload()
{
	setsToDownload.clear();
	for (int i = 0; i < allSets.size(); ++i)
		if (allSets[i].getImport())
			setsToDownload.append(allSets[i]);
	
	if (setsToDownload.isEmpty())
		return 0;
	setIndex = 0;
	emit setIndexChanged(0, 0, setsToDownload[0].getLongName());
	
	downloadNextFile();
	return setsToDownload.size();
}

void OracleImporter::downloadNextFile()
{
	QString urlString = setsToDownload[setIndex].getUrl();
	if (urlString.isEmpty())
		urlString = setUrl;
	urlString = urlString.replace("!longname!", setsToDownload[setIndex].getLongName());
	if (urlString.startsWith("http://")) {
		QUrl url(urlString);
		http->setHost(url.host(), QHttp::ConnectionModeHttp, url.port() == -1 ? 0 : url.port());
		QString path = QUrl::toPercentEncoding(urlString.mid(url.host().size() + 7).replace(' ', '+'), "?!$&'()*+,;=:@/");
		
		buffer->close();
		buffer->setData(QByteArray());
		buffer->open(QIODevice::ReadWrite | QIODevice::Text);
		reqId = http->get(path, buffer);
	} else {
		QFile file(dataDir + "/" + urlString);
		file.open(QIODevice::ReadOnly | QIODevice::Text);
		
		buffer->close();
		buffer->setData(file.readAll());
		buffer->open(QIODevice::ReadWrite | QIODevice::Text);
		reqId = 0;
		httpRequestFinished(reqId, false);
	}
}

void OracleImporter::httpRequestFinished(int requestId, bool error)
{
	if (error) {
		QMessageBox::information(0, tr("HTTP"), tr("Error."));
		return;
	}
	if (requestId != reqId)
		return;

	CardSet *set = new CardSet(setsToDownload[setIndex].getShortName(), setsToDownload[setIndex].getLongName());
	if (!setHash.contains(set->getShortName()))
		setHash.insert(set->getShortName(), set);
	
	buffer->seek(0);
	buffer->close();
	int cards = importTextSpoiler(set, buffer->data());
	++setIndex;
	
	if (setIndex == setsToDownload.size()) {
		emit setIndexChanged(cards, setIndex, QString());
		setIndex = -1;
	} else {
		downloadNextFile();
		emit setIndexChanged(cards, setIndex, setsToDownload[setIndex].getLongName());
	}
}

void OracleImporter::readResponseHeader(const QHttpResponseHeader &responseHeader)
{
	switch (responseHeader.statusCode()) {
		case 200:
		case 301:
		case 302:
		case 303:
		case 307:
			break;
		default:
			QMessageBox::information(0, tr("HTTP"), tr("Download failed: %1.").arg(responseHeader.reasonPhrase()));
			http->abort();
			deleteLater();
	}
}
