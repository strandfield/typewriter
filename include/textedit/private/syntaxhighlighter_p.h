// Copyright (C) 2018 Vincent Chambrin
// This file is part of the textedit library
// For conditions of distribution and use, see copyright notice in LICENSE

#ifndef TEXTEDIT_SYNTAXHIGHLIGHTER_P_H
#define TEXTEDIT_SYNTAXHIGHLIGHTER_P_H

#include "textedit/view/line.h"

namespace textedit
{

class SyntaxHighlighterImpl
{
public:
  view::Line line;
};

} // namespace textedit

#endif // !TEXTEDIT_SYNTAXHIGHLIGHTER_P_H