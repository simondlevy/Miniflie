{-# LANGUAGE DataKinds #-}
{-# LANGUAGE RebindableSyntax #-}

module ClimbRate where

import Language.Copilot
import Copilot.Compile.C99

import ClosedLoop
import Demands
import State
import Utils

runClimbRatePid :: SFloat -> SFloat -> SFloat
runClimbRatePid climbRate dz = kp * error + ki * integ

  where

    kp = 25
    ki = 15
    dt = 0.01

    integral_limit = 5000

    error = climbRate - dz

    integ = constrain (integ' + error * dt) (-integral_limit) integral_limit

    integ' = [0] ++ integ

------------------------------------------------------------------------------

newRunClimbRatePid :: ClosedLoopController

newRunClimbRatePid inHoverMode state demands = demands'  where

    kp = 25
    ki = 15
    dt = 0.01

    integral_limit = 5000

    thrust' = thrust demands

    error = thrust' - (dz state)

    integ = constrain (integ' + error * dt) (-integral_limit) integral_limit

    thrust'' = if inHoverMode then kp * error + ki * integ else thrust'

    demands' = Demands thrust''
                       (roll demands)
                       (pitch demands)
                       (yaw demands)
          
    integ' = [0] ++ integ
