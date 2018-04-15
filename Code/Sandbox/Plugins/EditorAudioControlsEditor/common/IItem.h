// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#pragma once

#include "SharedData.h"

#include <CryCore/Platform/platform.h>
#include <CryString/CryString.h>

namespace ACE
{
namespace Impl
{
struct IItem
{
	//! \cond INTERNAL
	virtual ~IItem() = default;
	//! \endcond

	//! Returns id of the item.
	virtual ControlId GetId() const = 0;

	//! Returns name of the item.
	virtual string GetName() const = 0;

	//! Returns file path of the item.
	//! If the item is not a file, an empty path is returned.
	virtual string const& GetFilePath() const = 0;

	//! Returns radius of the item.
	//! The radius is used to calculate the activity radius of the connected audio system trigger.
	virtual float GetRadius() const = 0;

	//! \return An integer of the sort priority, used for sort order in models.
	virtual int GetSortPriority() const = 0;

	//! Returns the number of children.
	virtual size_t GetNumChildren() const = 0;

	//! Returns a pointer to the child item at the given index.
	//! \param index Index of the child slot.
	virtual IItem* GetChildAt(size_t const index) const = 0;

	//! Returns a pointer to the parent item.
	virtual IItem* GetParent() const = 0;

	//! Returns flags of the item.
	virtual EItemFlags GetFlags() const = 0;
};
} // namespace Impl
} // namespace ACE
