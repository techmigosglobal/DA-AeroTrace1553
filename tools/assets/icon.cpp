#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QBuffer>
#include <QDataStream>
int main(int argc,char **argv){QGuiApplication app(argc,argv);if(argc!=4)return 2;QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));if(!svg.isValid())return 3;
  const QList<int> sizes={16,24,32,48,64,128,256};QList<QByteArray> png;
  for(int size:sizes){QImage image(size,size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);svg.render(&painter);painter.end();QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);if(!image.save(&buffer,"PNG"))return 4;png.append(bytes);if(size==256 && !image.save(QString::fromLocal8Bit(argv[3])))return 5;}
  QFile file(QString::fromLocal8Bit(argv[2]));if(!file.open(QIODevice::WriteOnly))return 6;QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);out<<quint16(0)<<quint16(1)<<quint16(sizes.size());quint32 offset=6+16*sizes.size();
  for(int i=0;i<sizes.size();++i){out<<quint8(sizes[i]==256?0:sizes[i])<<quint8(sizes[i]==256?0:sizes[i])<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(png[i].size())<<offset;offset+=png[i].size();}for(const auto &bytes:png)out.writeRawData(bytes.constData(),bytes.size());return out.status()==QDataStream::Ok?0:7;
}
