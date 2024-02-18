{--
  Altitude-hold algorithm for real and simulated flight controllers.
 
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

module Altitude where

import Language.Copilot
import Copilot.Compile.C99

import ClosedLoop
import Demands
import State
import Utils


altitudePid :: ClosedLoopController

altitudePid hover dt state demands = demands'  where

    kp = 2
    ki = 0.5
    ilimit = 5000

    thrustraw = thrust demands

    -- In hover mode, thrust demand comes in as [-1,+1], so
    -- we convert it to a target altitude in meters
    target = rescale thrustraw (-1) 1 0.2 2.0

    (thrustpid, integ) = piController kp ki dt ilimit target (z state) integ'

    integ' = [0] ++ integ
    
    thrustout = if hover then thrustpid else thrustraw

    demands' = Demands thrustout
                       (roll demands)
                       (pitch demands)
                       (yaw demands)
