{-# LANGUAGE DataKinds #-}
{-# LANGUAGE RebindableSyntax #-}

module Main where

import Language.Copilot
import Copilot.Compile.C99

-- import Prelude hiding ((>), (<), div, (++))

import Demands
import Mixers
import Motors
import State
import Utils

-- PID controllers
import Altitude
import ClimbRate
import PitchRollAngle
import PitchRollRate
import Position
import YawAngle
import YawRate

-- Constants that will be different for sim vs. actual -----------------------

thrust_base  = 48.0
thrust_scale = 0.25
thrust_min   = 0.0
thrust_max   = 60

yaw_scale = 4e-5

pitch_roll_scale = 1e-4

-- Streams from C++ ----------------------------------------------------------

demandsStruct :: Stream DemandsStruct
demandsStruct = extern "demands" Nothing

stateStruct :: Stream StateStruct
stateStruct = extern "state" Nothing

inHoverMode :: SBool
inHoverMode = extern "in_hover_mode" Nothing

-- Main ----------------------------------------------------------------------

spec = do

  let state = liftState stateStruct

  let demands = liftDemands demandsStruct

  ----------------------------------------------------------------------------

  let pids = [runAltitudePid,
              runClimbRatePid,
              runPositionPid,
              runPitchRollAnglePid, 
              runPitchRollRatePid, 
              runYawAnglePid, 
              runYawRatePid]

  let demands3 = foldl (\d f -> f state d) demands pids

  ----------------------------------------------------------------------------

  let thrust3 = (if inHoverMode then (thrust demands3) else thrust demands) * 
                (if inHoverMode then 1 else thrust_max)

  let thrust4 = constrain (thrust3 * thrust_scale + thrust_base)
                             thrust_min
                             thrust_max

  let roll2 = if inHoverMode then roll demands3 else (roll demands) * 30
  let pitch2 = if inHoverMode then pitch demands3 else (pitch demands) * 30

  let motors = quadCFMixer $ Demands thrust4 
                                     (roll2 * pitch_roll_scale)
                                     (pitch2 * pitch_roll_scale)
                                     ((yaw demands3) * yaw_scale)
  trigger "runMotors" true [
                       arg $ qm1 motors, 
                       arg $ qm2 motors, 
                       arg $ qm3 motors, 
                       arg $ qm4 motors
                     ] 

-- Compile the spec
main = reify spec >>= compile "copilot"
