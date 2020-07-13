// Copyright (C) 2020 Vincent Chambrin
// This file is part of the typewriter library
// For conditions of distribution and use, see copyright notice in LICENSE

#include "typewriter/textview.h"
#include "typewriter/private/textview_p.h"

#include "typewriter/view/fragment.h"
#include "typewriter/view/line.h"

#include <algorithm>
#include <cassert>
#include <iostream>

namespace typewriter
{

namespace view
{

std::string LineInfo::displayedText() const
{
  std::string r;

  for (const auto& e : this->elements)
  {
    if (e.kind == SimpleLineElement::LE_BlockFragment)
    {
      TextBlockIterator it = e.block.begin();
      it.seekColumn(e.begin);

      for (int i(0); i < e.width; ++i)
      {
        r += unicode::Utf8Char(it.current()).data();
        ++it;
      }
    }
  }

  return r;
}

BlockInfo::BlockInfo(const TextBlock& b)
  : block(b)
  , userstate(0)
  , revision(-1)
{

}

BlockInfo::~BlockInfo()
{

}

Fragment::Fragment()
{

}

Fragment::~Fragment()
{

}

Fragment::Fragment(BlockInfo const* line, int col, std::vector<FormatRange>::const_iterator iter, std::vector<FormatRange>::const_iterator sentinel, TextViewImpl const* view)
  : mLine(line)
  , mColumn(col)
  , mIterator(iter)
  , mSentinel(sentinel)
  , mView(view)
{

}

int Fragment::format() const
{
  return (mIterator == mSentinel || mColumn < mIterator->start) ? 0 : mIterator->format_id;
}

int Fragment::position() const
{
  return mColumn;
}

int Fragment::length() const
{
  return mIterator == mSentinel ? (mLine->block.length() - mColumn) : (mColumn < mIterator->start ? mIterator->start - mColumn : mIterator->length);
}

TextBlock Fragment::block() const
{
  return mLine->block;
}

std::string Fragment::text() const
{
  std::string ret = mLine->block.text().substr(position(), length());
  str_utils::replace_all(ret, '\t', mView->tabreplace);
  return ret;
}

Fragment Fragment::next() const
{
  if (mIterator == mSentinel)
    return Fragment{ mLine, mLine->block.length(), mSentinel, mSentinel, mView };
  else if (mColumn < mIterator->start)
    return Fragment(mLine, mIterator->start, mIterator, mSentinel, mView);
  else
    return Fragment(mLine, mColumn + mIterator->length, std::next(mIterator), mSentinel, mView);
}

bool Fragment::operator==(const Fragment& other) const
{
  return mLine == other.mLine && other.mColumn == mColumn;
}

bool Fragment::operator!=(const Fragment& other) const
{
  return mLine != other.mLine || other.mColumn != mColumn;
}

} // namespace view

TextViewImpl::TextViewImpl(TextDocument *doc)
  : document(doc)
{
  tabreplace = "    ";

  auto it = doc->firstBlock();

  std::shared_ptr<view::BlockInfo> prev = nullptr;

  do
  {
    auto block_info = std::make_shared<view::BlockInfo>(it);
    this->blocks[it.impl()] = block_info;

    if (prev)
    {
      block_info->prev = prev;
      prev->next = block_info;
    }

    prev = block_info;
    it = it.next();

  } while (it.isValid());

  Composer cmp{ this };
  cmp.relayout();
}

TextView::WrapMode TextViewImpl::computedWrapMode() const
{
  if (this->cpl <= 0)
    return TextView::WrapMode::NoWrap;
  return this->wrapmode;
}

void TextViewImpl::refreshLongestLineLength()
{
  this->longest_line_length = 0;

  for (auto& l : this->lines)
  {
    this->longest_line_length = std::max({ this->longest_line_length, l.width() });
  }
}

TextBlock TextViewImpl::getBlock(const view::LineInfo& l)
{
  for (const auto& e : l.elements)
  {
    if (e.block.isValid())
      return e.block;
  }

  assert(false);
  return TextBlock();
}

void Composer::Iterator::init(TextViewImpl* v)
{
  view = v;
  wrapmode = v->computedWrapMode();

  folds = v->folds.begin();
  inline_inserts = v->inline_inserts.begin();
  inserts = v->inserts.begin();
  textblock = v->document->firstBlock().begin();

  update();
}


void Composer::Iterator::update()
{
  if (current == BlockIterator)
  {
    if (folds != view->folds.end() && folds->cursor.selectionStart() == Position{ line, textblock.column() })
      current = FoldIterator;
    else if (inserts != view->inserts.end() && inserts->cursor.block() == textblock.block())
      current = InsertIterator;
    else if (inline_inserts != view->inline_inserts.end() && inline_inserts->cursor.block() == textblock.block() && inline_inserts->cursor.position().column == textblock.column())
      current = InlineInsertIterator;
  }
}

void Composer::Iterator::advance()
{
  if (current == FoldIterator)
  {
    const auto& fold = *folds;
    ++folds;

    assert(fold.cursor.position() == fold.cursor.selectionEnd());
    textblock = fold.cursor.block().begin();
    line = fold.cursor.selectionEnd().line;
    textblock.seekColumn(fold.cursor.selectionEnd().column);

    current = BlockIterator;
  }
  else if (current == InsertIterator)
  {
    ++insert_row;
    if (insert_row == inserts->span)
    {
      insert_row = 0;
      ++inserts;
      current = BlockIterator;
    }
  }
  else if (current == InlineInsertIterator)
  {
    const auto& inins = *inline_inserts;
    ++inline_inserts;
    textblock.seekColumn(inins.cursor.position().column);
    current = BlockIterator;
  }
  else if(current == BlockIterator)
  {
    int block_column = -1;
    int fold_column = -1;
    int inline_insert_column = -1;

    if (wrapmode == TextView::WrapMode::NoWrap)
    {
      block_column = textblock.block().length();
    }
    else if (wrapmode == TextView::WrapMode::Anywhere)
    {
      block_column = textblock.column() + 1;
    }
    else
    {
      if (isSpace() || isTab())
      {
        block_column = textblock.column() + 1;
      }
      else
      {
        block_column = textblock.column();
        auto utf8_iter = textblock.unicodeIterator();
        auto end_utf8_iter = unicode::utf8::end(textblock.block().text());

        while (utf8_iter != end_utf8_iter && *utf8_iter != ' ')
        {
          ++utf8_iter;
          ++block_column;
        }
      }
    }

    if (folds != view->folds.end() && folds->cursor.selectionStart().line == line)
    {
      fold_column = folds->cursor.selectionStart().column;
    }

    if (inline_inserts != view->inline_inserts.end() && inline_inserts->cursor.block() == textblock.block())
    {
      inline_insert_column = inline_inserts->cursor.position().column;
    }

    if (fold_column != -1 && (fold_column < inline_insert_column || inline_insert_column == -1) && fold_column < block_column)
    {
      textblock.seekColumn(fold_column);
      current = FoldIterator;
    }
    else if (inline_insert_column != -1 && (inline_insert_column < fold_column || fold_column == -1) && inline_insert_column < block_column)
    {
      textblock.seekColumn(inline_insert_column);
      current = InlineInsertIterator;
    }
    else
    {
      textblock.seekColumn(block_column);
      current = textblock.atEnd() ? LineFeedIterator : BlockIterator;
    }
  }
  else
  {
    assert(current == LineFeedIterator);

    textblock =  textblock.block().next().begin();
    line += 1;
    current = BlockIterator;
  }

  update();
}

bool Composer::Iterator::isSpace() const
{
  return current == BlockIterator && textblock.current() == ' ';
}

bool Composer::Iterator::isTab() const
{
  return current == BlockIterator && textblock.current() == '\t';
}

bool Composer::Iterator::atEnd() const
{
  return current == BlockIterator && !textblock.block().isValid();
}

int Composer::Iterator::currentWidth() const
{
  if (current == FoldIterator)
  {
    return folds->width;
  }
  else if (current == InsertIterator)
  {
    return 1;
  }
  else if (current == InlineInsertIterator)
  {
    return inline_inserts->span;
  }
  else
  {
    assert(current == BlockIterator);

    Iterator copy{ *this };
    copy.advance();

    if (copy.line == line)
      return copy.textblock.column() - textblock.column();
    else
      return textblock.block().length() - textblock.column();
  }
}

void Composer::Iterator::seek(const view::LineInfo& l)
{
  seek(TextViewImpl::getBlock(l));
}

void Composer::Iterator::seek(const TextBlock& b)
{
  textblock = b.begin();
  line = b.blockNumber();

  Position pos{ line, textblock.column() };

  folds = std::lower_bound(view->folds.begin(), view->folds.end(), pos, [](const SimpleTextFold& lhs, const Position& rhs) -> bool {
    return lhs.cursor.selectionStart().line < rhs.line
      || (lhs.cursor.selectionStart().line == rhs.line && lhs.cursor.selectionStart().column < rhs.column);
    });

  inserts = std::lower_bound(view->inserts.begin(), view->inserts.end(), pos, [](const view::Insert& lhs, const Position& rhs) -> bool {
    return lhs.cursor.selectionStart().line < rhs.line
      || (lhs.cursor.selectionStart().line == rhs.line && lhs.cursor.selectionStart().column < rhs.column);
    });

  inline_inserts = std::lower_bound(view->inline_inserts.begin(), view->inline_inserts.end(), pos, [](const view::InlineInsert& lhs, const Position& rhs) -> bool {
    return lhs.cursor.selectionStart().line < rhs.line
      || (lhs.cursor.selectionStart().line == rhs.line && lhs.cursor.selectionStart().column < rhs.column);
    });
}

Composer::Composer(TextViewImpl* v)
  : view(v)
{
  iterator.init(v);
}

void Composer::relayout()
{
  view->lines.clear();
  has_invalidate_longest_line = true;

  longest_line_width = 0;
  current_line.clear();
  current_block = view->document->firstBlock();
  line_iterator = view->lines.end();

  iterator.init(view);

  while (current_block.isValid())
  {
    relayoutBlock();
  }

  checkLongestLine();
}

void Composer::relayoutBlock()
{
  // TODO: wrap words

  int cpl = view->cpl <= 0 ? std::numeric_limits<int>::max() : view->cpl;

  if (view->wrapmode == TextView::WrapMode::NoWrap)
    cpl = std::numeric_limits<int>::max();

  while (iterator.current != LineFeedIterator)
  {
    if (iterator.current == InsertIterator)
    {
      current_line.push_back(createLineElement(iterator));
      writeCurrentLine();
      iterator.advance();
      continue;
    }

    int cur_width = iterator.currentWidth();

    if (current_line_width + cur_width <= cpl)
    {
      current_line.push_back(createLineElement(iterator, cur_width));
      current_line_width += cur_width;
      iterator.advance();
    }
    else
    {
      if ((view->wrapmode == TextView::WrapMode::WordBoundaryOrAnywhere && iterator.current == Composer::BlockIterator) || cur_width > cpl)
      {
        int diff = cpl - current_line_width;
        current_line.push_back(createLineElement(iterator, diff));
        current_line_width += diff;
        current_line.push_back(createCarriageReturn());
        writeCurrentLine();
        current_line.push_back(createLineIndent());

        iterator.textblock.seekColumn(iterator.textblock.column() + diff);
      }
      else
      {
        current_line.push_back(createCarriageReturn());
        writeCurrentLine();
        current_line.push_back(createLineIndent());

        if (iterator.isSpace())
          iterator.advance();
      }
    }
  }

  writeCurrentLine();
  iterator.advance();

  while (line_iterator != view->lines.end() && TextViewImpl::getBlock(*line_iterator) == current_block)
  {
    line_iterator = view->lines.erase(line_iterator);
  }

  updateBlockLineIterator(current_block, iterator.textblock.block());
  current_block = iterator.textblock.block();

  // A fold may have hidden some lines that need to be destroyed,
  // it may also (if deleted) have restored some lines
  // TODO: we need to have more information, i.e. know if a fold was added or removed
  while (line_iterator != view->lines.end())
  {
    TextBlock block = TextViewImpl::getBlock(*line_iterator);

    if (block != current_block)
    {
      if (line_iterator->width() == view->longest_line_length)
        has_invalidate_longest_line = true;

      line_iterator = view->lines.erase(line_iterator);
    }
    else
    {
      break;
    }
  }
}

void Composer::checkLongestLine()
{
  if (longest_line_width < view->longest_line_length && has_invalidate_longest_line)
  {
    view->refreshLongestLineLength();
  }
  else if (longest_line_width > view->longest_line_length)
  {
    view->longest_line_length = longest_line_width;
  }
}

void Composer::writeCurrentLine()
{
  if (current_line_width > longest_line_width)
  {
    longest_line_width = current_line_width;
  }

  if (line_iterator != view->lines.end() && TextViewImpl::getBlock(*line_iterator) == current_block)
  {
    if (line_iterator->width() == view->longest_line_length)
      has_invalidate_longest_line = true;

    std::swap(line_iterator->elements, current_line);

    if(line_iterator->elements.front().id != view::SimpleLineElement::LE_LineIndent)
      view->blocks[current_block.impl()]->line = line_iterator;

    ++line_iterator;
  }
  else
  {
    assert(line_iterator == view->lines.end() || TextViewImpl::getBlock(*line_iterator) != current_block);

    line_iterator = view->lines.insert(line_iterator, view::LineInfo{ std::move(current_line) });

    if (line_iterator->elements.front().id != view::SimpleLineElement::LE_LineIndent)
      view->blocks[current_block.impl()]->line = line_iterator;

    ++line_iterator;
  }

  current_line.clear();
  current_line_width = 0;
}

void Composer::updateBlockLineIterator(TextBlock begin, TextBlock end)
{
  auto lit = std::prev(line_iterator);

  while (lit->elements.front().id == view::SimpleLineElement::LE_LineIndent)
    --lit;

  std::shared_ptr<view::BlockInfo> info = view->blocks[begin.impl()];

  while (info && info->block != end)
  {
    info->line = lit;
    info = info->next.lock();
  }
}

void Composer::relayout(TextBlock b)
{
  auto it = getLine(b);

  if (it == view->lines.end())
    return;

  relayout(it);
}

std::list<view::LineInfo>::iterator Composer::getLine(TextBlock b)
{
  auto it = view->blocks.find(b.impl());

  if (it == view->blocks.end())
    return {};

  std::shared_ptr<view::BlockInfo> info = it->second;

  if (info == nullptr)
    return {};

  return info->line;
}

void Composer::relayout(std::list<view::LineInfo>::iterator it)
{
  line_iterator = it;
  current_block = TextViewImpl::getBlock(*line_iterator);

  iterator.seek(*line_iterator);

  relayoutBlock();

  checkLongestLine();
}

void Composer::handleBlockInsertion(const TextBlock& b)
{
  auto it = getLine(b.previous());
  auto next = it;

  while (next != view->lines.end() && TextViewImpl::getBlock(*next) == b.previous())
  {
    ++next;
  }

  if (next == view->lines.end())
  {
    current_block = b;
    line_iterator = view->lines.end();
    relayoutBlock();
  }
  else
  {
    if (TextViewImpl::getBlock(*next) == b.next())
    {
      // 'b' is not inside a fold

      current_block = b;
      line_iterator = next;
      relayoutBlock();
    }
  }

  checkLongestLine();
}

void Composer::handleBlockRemoval(const TextBlock& b)
{
  auto it = getLine(b);

  while (it != view->lines.end() && TextViewImpl::getBlock(*it) == b)
  {
    it = view->lines.erase(it);
  }

  relayout(b.previous());
}

void Composer::handleFoldInsertion(std::vector<SimpleTextFold>::iterator it)
{
  TextBlock start_block = prev(it->cursor.block(), it->cursor.position().line - it->cursor.anchor().line);
  relayout(start_block);
}

void Composer::handleFoldRemoval(const TextCursor& sel)
{
  TextBlock start_block = prev(sel.block(), sel.position().line - sel.anchor().line);
  TextBlock end_block = sel.block().next();

  iterator.seek(start_block);
  current_block = start_block;

  line_iterator = getLine(start_block);

  while (current_block != end_block)
  {
    relayoutBlock();
  }

  checkLongestLine();
}

view::SimpleLineElement Composer::createLineElement(const Iterator& it, int w)
{
  view::SimpleLineElement e;

  w = w == -1 ? it.currentWidth() : w;

  switch (it.current)
  {
  case FoldIterator:
  {
    e.kind = view::SimpleLineElement::LE_Fold;
    e.width = w;
    e.id = it.folds->id;
  }
  break;
  case InsertIterator:
  {
    e.kind = view::SimpleLineElement::LE_Insert;
    e.width = w;
    e.nbrow = it.insert_row;
  }
  break;
  case InlineInsertIterator:
  {
    e.kind = view::SimpleLineElement::LE_InlineInsert;
    e.width = w;
  }
  break;
  case BlockIterator:
  {
    e.kind = view::SimpleLineElement::LE_BlockFragment;
    e.width = w;
    e.block = it.textblock.block();
    e.begin = it.textblock.column();
  }
  break;
  }

  return e;
}

view::SimpleLineElement Composer::createCarriageReturn()
{
  view::SimpleLineElement e;
  e.kind = view::SimpleLineElement::LE_CarriageReturn;
  e.width = 0;
  return e;
}

view::SimpleLineElement Composer::createLineIndent()
{
  view::SimpleLineElement e;
  e.kind = view::SimpleLineElement::LE_LineIndent;
  e.width = 0;
  return e;
}

TextView::TextView(TextDocument *document)
  : d(new TextViewImpl(document))
{
  init();
}

TextView::TextView(std::unique_ptr<TextViewImpl> && impl)
  : d(std::move(impl))
{
  init();
}

TextView::~TextView()
{
  document()->removeListener(this);
}

void TextView::init()
{
  document()->addListener(this);

  d->refreshLongestLineLength();
}

TextDocument* TextView::document() const
{
  return d->document;
}

void TextView::setTabSize(int n)
{
  if (tabSize() == n)
    return;

  d->tabreplace.clear();

  while (n > 0)
  {
    d->tabreplace.push_back(' ');
    --n;
  }

  Composer cmp{ d.get() };
  cmp.relayout(); // TODO: avoid cleaning everything
}

int TextView::charactersPerLine() const
{
  return d->cpl;
}

void TextView::setCharactersPerLine(int n)
{
  if (d->cpl != n)
  {
    d->cpl = n;
    
    Composer cmp{ d.get() };
    cmp.relayout(); // TODO: avoid cleaning everything
  }
}

int TextView::tabSize() const
{
  return d->tabreplace.size();
}

int TextView::height() const
{
  return lines().size();
}

int TextView::width() const
{
  return d->longest_line_length;
}

const std::list<view::LineInfo>& TextView::lines() const
{
  return d->lines;
}

TextView::WrapMode TextView::wrapMode() const
{
  return d->wrapmode;
}

void TextView::setWrapMode(WrapMode wm)
{
  if (wm != d->wrapmode)
  {
    d->wrapmode = wm;

    Composer cmp{ d.get() };
    cmp.relayout(); // TODO: avoid cleaning everything
  }
}

void TextView::addFold(int id, TextCursor sel, int w)
{
  if (sel.anchor() > sel.position())
  {
    auto p = sel.anchor();
    sel.setPosition(sel.position(), TextCursor::MoveAnchor);
    sel.setPosition(p, TextCursor::KeepAnchor);
  }

  assert(sel.anchor() < sel.position());

  auto it = std::lower_bound(d->folds.begin(), d->folds.end(), sel.anchor(), [](const SimpleTextFold& lhs, const Position& rhs) -> bool {
    return lhs.cursor.anchor() < rhs;
    });

  SimpleTextFold stf;
  stf.cursor = sel;
  stf.id = id;
  stf.width = w;

  it = d->folds.insert(it, stf);

  Composer composer{ d.get() };
  composer.handleFoldInsertion(it);
}

void TextView::removeFold(int id)
{
  auto it = std::find_if(d->folds.begin(), d->folds.end(), [id](const SimpleTextFold& f) -> bool {
    return f.id == id;
    });

  if (it == d->folds.end())
    return;

  SimpleTextFold fold = *it;

  d->folds.erase(it);

  Composer composer{ d.get() };
  composer.handleFoldRemoval(fold.cursor);
}

void TextView::clearFolds()
{
  while (!d->folds.empty())
  {
    SimpleTextFold f = d->folds.back();
    d->folds.pop_back();

    Composer composer{ d.get() };
    composer.handleFoldRemoval(f.cursor);
  }
}

void TextView::addInsert(view::Insert ins)
{
  auto it = std::lower_bound(d->inserts.begin(), d->inserts.end(), ins, 
    [](const view::Insert& lhs, const view::Insert& rhs) -> bool {
      return lhs.cursor.position() < rhs.cursor.position();
    });

  it = d->inserts.insert(it, ins);

  Composer composer{ d.get() };
  composer.relayout(ins.cursor.block());
}

void TextView::addInlineInsert(view::InlineInsert ins)
{
  auto it = std::lower_bound(d->inline_inserts.begin(), d->inline_inserts.end(), ins, 
    [](const view::InlineInsert& lhs, const view::InlineInsert& rhs) -> bool {
      return lhs.cursor.position() < rhs.cursor.position();
    });

  it = d->inline_inserts.insert(it, ins);

  Composer composer{ d.get() };
  composer.relayout(ins.cursor.block());
}

void TextView::clearInserts()
{
  while (!d->inserts.empty())
  {
    view::Insert ins = d->inserts.back();
    d->inserts.pop_back();

    Composer composer{ d.get() };
    composer.relayout(ins.cursor.block());
  }

  while (!d->inline_inserts.empty())
  {
    view::InlineInsert ins = d->inline_inserts.back();
    d->inline_inserts.pop_back();

    Composer composer{ d.get() };
    composer.relayout(ins.cursor.block());
  }
}

const std::vector<view::Insert>& TextView::inserts() const
{
  return d->inserts;
}

const std::vector<view::InlineInsert>& TextView::inlineInserts() const
{
  return d->inline_inserts;
}

void TextView::blockDestroyed(int line, const TextBlock & block)
{
  Composer cmp{ d.get() };
  cmp.handleBlockRemoval(block);

  auto it = d->blocks.find(block.impl());

  std::shared_ptr<view::BlockInfo> info = it->second;

  auto prev_info = info->prev.lock();
  auto next_info = info->next.lock();

  prev_info->next = next_info;

  if (next_info)
    next_info->prev = prev_info;

  d->blocks.erase(it);
}

void TextView::blockInserted(const Position & pos, const TextBlock & block)
{
  Composer cmp{ d.get() };
  cmp.handleBlockInsertion(block);

  auto info = std::make_shared<view::BlockInfo>(block);
  d->blocks[block.impl()] = info;

  auto prev_info = d->blocks[block.previous().impl()];

  auto next_info = prev_info->next.lock();

  if (next_info)
  {
    next_info->prev = info;
    info->next = next_info;
  }

  prev_info->next = info;
  info->prev = prev_info;
}

void TextView::contentsChange(const TextBlock& block, const Position& pos, int charsRemoved, int charsAdded)
{
  Composer cmp{ d.get() };
  cmp.relayout(block);
}

std::string TextView::replaceTabs(std::string text) const
{
  str_utils::replace_all(text, '\t', d->tabreplace);
  return text;
}

} // namespace typewriter
