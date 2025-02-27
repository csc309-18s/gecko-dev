/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ServoStyleRuleMap_h
#define mozilla_ServoStyleRuleMap_h

#include "mozilla/ServoStyleRule.h"

#include "nsDataHashtable.h"

struct RawServoStyleRule;
class nsXBLPrototypeResources;

namespace mozilla {
class ServoCSSRuleList;
class ServoStyleSet;
namespace css {
class Rule;
} // namespace css
namespace dom {
class ShadowRoot;
}
class ServoStyleRuleMap
{
public:
  ServoStyleRuleMap() = default;

  void EnsureTable(ServoStyleSet&);
  void EnsureTable(nsXBLPrototypeResources&);
  void EnsureTable(dom::ShadowRoot&);

  ServoStyleRule* Lookup(const RawServoStyleRule* aRawRule) const
  {
    return mTable.Get(aRawRule);
  }

  void SheetAdded(ServoStyleSheet&);
  void SheetRemoved(ServoStyleSheet&);

  void RuleAdded(ServoStyleSheet& aStyleSheet, css::Rule&);
  void RuleRemoved(ServoStyleSheet& aStyleSheet, css::Rule&);

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  ~ServoStyleRuleMap() = default;

private:
  // Since we would never have a document which contains no style rule,
  // we use IsEmpty as an indication whether we need to walk through
  // all stylesheets to fill the table.
  bool IsEmpty() const { return mTable.Count() == 0; }

  void FillTableFromRule(css::Rule& aRule);
  void FillTableFromRuleList(ServoCSSRuleList& aRuleList);
  void FillTableFromStyleSheet(ServoStyleSheet& aSheet);

  typedef nsDataHashtable<nsPtrHashKey<const RawServoStyleRule>,
                          WeakPtr<ServoStyleRule>> Hashtable;
  Hashtable mTable;
};

} // namespace mozilla

#endif // mozilla_ServoStyleRuleMap_h
