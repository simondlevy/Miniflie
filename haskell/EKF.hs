{--
  EKF algorithm for Crazyflie
 
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

module Main where

import Language.Copilot
import Copilot.Compile.C99

import Utils

------------------------------------------------------------------------------

type EkfMode = SInt8

ekfMode :: EkfMode
ekfMode = extern "stream_ekfMode" Nothing

mode_init      = 0 :: EkfMode
mode_predict   = 1 :: EkfMode
mode_update    = 2 :: EkfMode
mode_finalize  = 3 :: EkfMode
mode_get_state = 4 :: EkfMode

------------------------------------------------------------------------------

data Axis3f = Axis3f {

    x :: SFloat
  , y :: SFloat
  , z :: SFloat

}

initAxis3f :: SBool -> Axis3f -> Axis3f

initAxis3f init axis3f = Axis3f x' y' z' where

  x' = if init then 0 else (x axis3f)
  y' = if init then 0 else (y axis3f)
  z' = if init then 0 else (z axis3f)
  

------------------------------------------------------------------------------

data Axis3fSubSampler = Axis3fSubSampler {

     summ :: Axis3f
   , count :: SInt32
   , conversionFactor :: SFloat
   , subSample :: Axis3f

}

initAxis3fSubSampler :: SBool -> Axis3fSubSampler -> Axis3fSubSampler

initAxis3fSubSampler init a3fss = a3fss' where

  a3fss' = Axis3fSubSampler summ' count' conversionFactor' subSample'

  summ' = summ a3fss
  count' = count a3fss
  conversionFactor' = conversionFactor a3fss
  subSample' = subSample a3fss'
 



------------------------------------------------------------------------------

data Quat = Quat { 
    qw :: SFloat
  , qx :: SFloat
  , qy :: SFloat
  , qz :: SFloat 
}

initQuat :: SBool -> Quat -> Quat

initQuat init quat = Quat qw' qx' qy' qz' where

  qw' = if init then 1 else (qw quat)
  qx' = if init then 0 else (qx quat)
  qy' = if init then 0 else (qy quat)
  qz' = if init then 0 else (qz quat)

------------------------------------------------------------------------------

data EkfState = EkfState {

    zz :: SFloat
  , dx :: SFloat
  , dy :: SFloat
  , dz :: SFloat
  , e0 :: SFloat
  , e1 :: SFloat
  , e2 :: SFloat
}

initEkfState :: SBool -> EkfState -> EkfState

initEkfState init ekfState = EkfState zz' dx' dy' dz' e0' e1' e2' where

  zz' = if init then 0 else (zz ekfState)
  dx' = if init then 0 else (dx ekfState)
  dy' = if init then 0 else (dy ekfState)
  dz' = if init then 0 else (dz ekfState)
  e0' = if init then 0 else (e0 ekfState)
  e1' = if init then 0 else (e1 ekfState)
  e2' = if init then 0 else (e2 ekfState)

------------------------------------------------------------------------------

data Ekf = Ekf { 

    ekfState :: EkfState

  , quat :: Quat

    -- misc
  , lastPredictionMsec :: SInt32
  , lastProcessNoiesUpdateMsec :: SInt32
  , isUpdated :: SBool

  -- third row (Z) of attitude as a rotation matrix for prediction
  , r20 :: SFloat
  , r21 :: SFloat
  , r22 :: SFloat
}

------------------------------------------------------------------------------

runEkf :: SInt32 -> Ekf

runEkf nowMsec = Ekf ekfState
                     quat
                     lastPredictionMsec lastProcessNoiseUpdateMsec isUpdated 
                     r20 r21 r22

   where 

         init = ekfMode == mode_init

         ekfState = initEkfState init (EkfState zz' dx' dy' dz' e0' e1' e2')

         quat = initQuat init (Quat qw' qx' qy' qz')

         isUpdated = if init then false else isUpdated'

         lastPredictionMsec = if init then nowMsec else lastPredictionMsec'

         lastProcessNoiseUpdateMsec = if init then nowMsec 
                                      else lastProcessNoiseUpdateMsec'

         r20 = if init then 0 else r20'
         r21 = if init then 0 else r20'
         r22 = if init then 1 else r20'

         qw' = [1] ++ (qw quat)
         qx' = [0] ++ (qx quat)
         qy' = [0] ++ (qy quat)
         qz' = [0] ++ (qz quat)

         zz' = [0] ++ (zz ekfState)
         dx' = [0] ++ (dx ekfState)
         dy' = [0] ++ (dy ekfState)
         dz' = [0] ++ (dz ekfState)
         e0' = [0] ++ (e0 ekfState)
         e1' = [0] ++ (e1 ekfState)
         e2' = [0] ++ (e2 ekfState)

         lastPredictionMsec' = [0] ++ lastPredictionMsec

         lastProcessNoiseUpdateMsec' = [0] ++ lastProcessNoiseUpdateMsec

         isUpdated' = [False] ++ isUpdated

         -- Set the initial rotation matrix to the identity. This only affects
         -- the first prediction step, since in the finalization, after 
         -- shifting attitude errors into the attitude state, the rotation
         -- matrix is updated.
         r20' = [0] ++ r20
         r21' = [0] ++ r21
         r22' = [1] ++ r22

------------------------------------------------------------------------------

spec = do

    trigger "dummy" true [ ]

-- Compile the spec
main = reify spec >>= 
  compileWith (CSettings "copilot_step_ekf" ".") "copilot_ekf"
