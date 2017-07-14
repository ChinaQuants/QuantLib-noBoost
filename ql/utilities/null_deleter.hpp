/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2016 Peter Caspers

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file null_deleter.hpp
    \brief empty deleter for shared_ptr
*/

#ifndef quantlib_nulldeleter_hpp
#define quantlib_nulldeleter_hpp

#include <ql/qldefines.hpp>

namespace QuantLib {
#ifndef _MSC_VER
    inline auto null_deleter = [](auto*)noexcept {};
#else
    //Visual Studio does not support inline variable
    static auto null_deleter = [](auto*)noexcept {};	
#endif
}


#endif
