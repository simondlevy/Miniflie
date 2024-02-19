{--
  Utility functions for real and simulated flight controllers
 
  Copyright (C) 2024 Simon D. Levy
 
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, in version 3.
 
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
--} 

{-# LANGUAGE DataKinds #-}
{-# LANGUAGE RebindableSyntax #-}

module Utils where

import Language.Copilot
import Copilot.Compile.C99

type SFloat = Stream Float
type SBool = Stream Bool

rescale :: SFloat -> SFloat -> SFloat -> SFloat -> SFloat -> SFloat
rescale value oldmin oldmax newmin newmax =             
  (value - oldmin) / (oldmax - oldmin) * (newmax - newmin) + newmin

constrain :: SFloat -> SFloat -> SFloat -> SFloat
constrain val min max = if val < min then min else if val > max then max else val

rad2deg :: SFloat -> SFloat
rad2deg rad = 180 * rad / pi

deg2rad :: SFloat -> SFloat
deg2rad deg = deg * pi / 180