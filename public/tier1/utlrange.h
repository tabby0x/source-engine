//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Templates to support creating generic iterator and range types
//
//=============================================================================//

#ifndef UTLRANGE_H
#define UTLRANGE_H

#ifdef _WIN32
#pragma once
#endif

// This breaks <limits>/numeric_limits on MSVC.
#if defined( min ) || defined( max )
#define UTLRANGE_H_MINMAX_QUIRK
#include "valve_minmax_off.h"
#endif

#include <limits>

//-----------------------------------------------------------------------------
// A simple index range that supports range-for iteration.
//-----------------------------------------------------------------------------
template < typename VALUE_TYPE = int, typename OUTER_TYPE = void >
class CUtlIndexRange
{
public:
	typedef VALUE_TYPE ValueType_t;

	CUtlIndexRange() {}

	CUtlIndexRange( VALUE_TYPE nBegin, VALUE_TYPE nEnd )
		: m_begin( nBegin ), m_end( nEnd )
	{
	}

	struct index
	{
	public:
		index &operator++() { value++; return *this; }
		index operator++( int ) { index ret = *this; value++; return ret; }
		bool operator==( index b ) const { return value == b.value; }
		bool operator!=( index b ) const { return value != b.value; }
		bool operator>( index b ) const { return value > b.value; }
		bool operator>=( index b ) const { return value >= b.value; }
		bool operator<( index b ) const { return value < b.value; }
		bool operator<=( index b ) const { return value <= b.value; }
		index &operator*() { return *this; }

		VALUE_TYPE value;
	};

	index begin() const { return index{ m_begin }; }
	index end() const { return index{ m_end }; }

	bool BValidIdx( index i ) const { return i >= begin() && i < end(); }

private:
	const VALUE_TYPE m_begin = std::numeric_limits< VALUE_TYPE >::min();
	const VALUE_TYPE m_end = std::numeric_limits< VALUE_TYPE >::max();
};

#ifdef UTLRANGE_H_MINMAX_QUIRK
#undef UTLRANGE_H_MINMAX_QUIRK
#include "valve_minmax_on.h"
#endif

#endif // UTLRANGE_H
