// Copyright (C) 2018 Vincent Chambrin
// This file is part of the liblayout project
// For conditions of distribution and use, see copyright notice in LICENSE

#include <QApplication>

#include "textedit/syntaxhighlighter.h"
#include "textedit/textdocument.h"
#include "textedit/textview.h"

#include <QMouseEvent>
#include <QVBoxLayout>

class CustomSyntaxHighlighter : public textedit::SyntaxHighlighter
{
public:

  static QSet<QString> getKeywords()
  {
    QSet<QString> result;
    result << "bool" << "char" << "int" << "return";
    return result;
  }

  static textedit::TextFormat keywordFormat()
  {
    textedit::TextFormat fmt;
    fmt.setTextColor(QColor(100, 100, 255));
    return fmt;
  }

  static textedit::TextFormat underlineFormat()
  {
    textedit::TextFormat fmt;
    fmt.setUnderlineStyle(textedit::TextFormat::DashDotLine);
    fmt.setUnderlineColor(Qt::red);
    return fmt;
  }

  static textedit::TextFormat waveUnderlineFormat()
  {
    textedit::TextFormat fmt;
    fmt.setUnderlineStyle(textedit::TextFormat::WaveUnderline);
    fmt.setUnderlineColor(Qt::green);
    return fmt;
  }

  static textedit::TextFormat strikeOutFormat()
  {
    textedit::TextFormat fmt;
    fmt.setStrikeOut();
    fmt.setStrikeOutColor(Qt::blue);
    return fmt;
  }

  void highlightBlock(const QString & text) override
  {
    static const QSet<QString> keywords = getKeywords();

    QStringList tokens = text.split(" ", QString::KeepEmptyParts);
    int offset = 0;
    for (auto t : tokens)
    {
      if (keywords.contains(t))
        setFormat(offset, t.length(), keywordFormat());
      else if (t == "underline")
        setFormat(offset, t.length(), underlineFormat());
      else if (t == "wave_underline")
        setFormat(offset, t.length(), waveUnderlineFormat());
      else if (t == "strikeout")
        setFormat(offset, t.length(), strikeOutFormat());
      offset += t.length() + 1;
    }
  }

  bool usesBlockState() const override
  {
    return false;
  }
};

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);

  using namespace textedit;

  QStringList lines;
  lines << "int underline = 0;"
    << "int abs(int a)"
    << "{"
    << "  return a < 0 ? -a : a;"
    << "}"
    << "bool wave_underline = false;"
    << ""
    << "strikeout ();";

  TextDocument *doc = new TextDocument{ lines.join("\n") };

  TextView *view = new TextView(doc);
  doc->setParent(view);
  view->setSyntaxHighlighter<CustomSyntaxHighlighter>();

  view->resize(400, 200);
  view->show();

  return app.exec();
}

#include "main.moc"