object FormMotecSettings: TFormMotecSettings
  Left = 0
  Top = 0
  Caption = 'Threshold Settings'
  ClientHeight = 500
  ClientWidth = 600
  Color = clWhite
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'Segoe UI'
  Font.Style = []
  Position = poScreenCenter
  OnShow = FormShow
  TextHeight = 15
  object PageControl: TPageControl
    Left = 8
    Top = 8
    Width = 584
    Height = 430
    ActivePage = TabTyre
    TabHeight = 25
    TabOrder = 0
    TabWidth = 80
    object TabGeneral: TTabSheet
      Caption = 'General'
      object GroupGeneral: TGroupBox
        Left = 21
        Top = 21
        Width = 552
        Height = 160
        Caption = 'Sampling & Confidence'
        TabOrder = 0
        object LblMinDamperSamples: TLabel
          Left = 16
          Top = 32
          Width = 116
          Height = 15
          Caption = 'Min Damper Samples:'
        end
        object LblMinHandlingSamples: TLabel
          Left = 16
          Top = 64
          Width = 123
          Height = 15
          Caption = 'Min Handling Samples:'
        end
        object LblMinSteerYawSamples: TLabel
          Left = 16
          Top = 96
          Width = 126
          Height = 15
          Caption = 'Min Steer/Yaw Samples:'
        end
        object LblHandlingConfidenceSamples: TLabel
          Left = 16
          Top = 128
          Width = 182
          Height = 15
          Caption = 'Min Samples for High Confidence:'
        end
        object EditMinDamperSamples: TEdit
          Left = 212
          Top = 30
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '20'
        end
        object EditMinHandlingSamples: TEdit
          Left = 212
          Top = 61
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '8'
        end
        object EditMinSteerYawSamples: TEdit
          Left = 212
          Top = 93
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '20'
        end
        object EditHandlingConfidenceSamples: TEdit
          Left = 212
          Top = 125
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '30'
        end
      end
      object GroupLapValidity: TGroupBox
        Left = 21
        Top = 200
        Width = 552
        Height = 90
        Caption = 'Valid Lap Time'
        TabOrder = 1
        object LblMinValidLapTime: TLabel
          Left = 16
          Top = 32
          Width = 145
          Height = 15
          Caption = 'Min Valid Lap Time (x best):'
        end
        object LblMaxValidLapTime: TLabel
          Left = 16
          Top = 64
          Width = 147
          Height = 15
          Caption = 'Max Valid Lap Time (x best):'
        end
        object EditMinValidLapTime: TEdit
          Left = 212
          Top = 30
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '0.7'
        end
        object EditMaxValidLapTime: TEdit
          Left = 212
          Top = 62
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '1.3'
        end
      end
    end
    object TabTyre: TTabSheet
      Caption = 'Tyre'
      object GroupTyrePressure: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 180
        Caption = 'Pressure (psi)'
        TabOrder = 0
        object LblPressLow: TLabel
          Left = 16
          Top = 32
          Width = 25
          Height = 15
          Caption = 'Low:'
        end
        object LblPressHigh: TLabel
          Left = 16
          Top = 64
          Width = 29
          Height = 15
          Caption = 'High:'
        end
        object LblPressSideDiffMax: TLabel
          Left = 220
          Top = 68
          Width = 151
          Height = 15
          Caption = 'Max Left/Right Pressure Diff:'
        end
        object LblPressFrontRearDiffMax: TLabel
          Left = 220
          Top = 32
          Width = 154
          Height = 15
          Caption = 'Max Front/Rear Pressure Diff:'
        end
        object EditPressLow: TEdit
          Left = 64
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '23.0'
        end
        object EditPressHigh: TEdit
          Left = 64
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '33.0'
        end
        object EditPressSideDiffMax: TEdit
          Left = 386
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '1.5'
        end
        object EditPressFrontRearDiffMax: TEdit
          Left = 386
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '2.0'
        end
      end
      object GroupTyreTemp: TGroupBox
        Left = 16
        Top = 204
        Width = 552
        Height = 210
        Caption = 'Temperature (C)'
        TabOrder = 1
        object LblCamberMinSpread: TLabel
          Left = 12
          Top = 32
          Width = 128
          Height = 15
          Caption = 'Min Inner/Outer Spread:'
        end
        object LblCamberMaxSpread: TLabel
          Left = 12
          Top = 61
          Width = 130
          Height = 15
          Caption = 'Max Inner/Outer Spread:'
        end
        object EditCamberMinSpread: TEdit
          Left = 165
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '3.5'
        end
        object EditCamberMaxSpread: TEdit
          Left = 165
          Top = 57
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '6.5'
        end
      end
    end
    object TabHandling: TTabSheet
      Caption = 'Handling'
      object GroupHandling: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 384
        Caption = 'Detection Thresholds'
        TabOrder = 0
        object LblBrakePedalMin: TLabel
          Left = 16
          Top = 32
          Width = 88
          Height = 15
          Caption = 'Brake Pedal Min:'
        end
        object LblThrottleMaxBraking: TLabel
          Left = 16
          Top = 64
          Width = 113
          Height = 15
          Caption = 'Throttle Max Braking:'
        end
        object LblSteerMinCornering: TLabel
          Left = 16
          Top = 96
          Width = 109
          Height = 15
          Caption = 'Steer Min Cornering:'
        end
        object EditBrakePedalMin: TEdit
          Left = 170
          Top = 29
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '10.0'
        end
        object EditThrottleMaxBraking: TEdit
          Left = 170
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '35.0'
        end
        object EditSteerMinCornering: TEdit
          Left = 170
          Top = 89
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '5.0'
        end
      end
    end
    object TabThermal: TTabSheet
      Caption = 'Thermal'
      object GroupThermal: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 384
        Caption = 'Engine Thermal Thresholds'
        TabOrder = 0
        object LblWaterTempMax: TLabel
          Left = 16
          Top = 32
          Width = 92
          Height = 15
          Caption = 'Max Water Temp:'
        end
        object LblOilTempMax: TLabel
          Left = 16
          Top = 64
          Width = 76
          Height = 15
          Caption = 'Max Oil Temp:'
        end
        object EditWaterTempMax: TEdit
          Left = 170
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '95.0'
        end
        object EditOilTempMax: TEdit
          Left = 170
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '115.0'
        end
      end
    end
    object TabSuspension: TTabSheet
      Caption = 'Suspension'
      object GroupDamper: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 178
        Caption = 'Damper Velocity'
        TabOrder = 0
        object LblDamperLowSpeedThreshold: TLabel
          Left = 16
          Top = 32
          Width = 115
          Height = 15
          Caption = 'Low Speed Threshold:'
        end
        object LblDamperAsymmetryThreshold: TLabel
          Left = 16
          Top = 64
          Width = 119
          Height = 15
          Caption = 'Asymmetry Threshold:'
        end
        object LblDamperHighSpeedShareMax: TLabel
          Left = 16
          Top = 96
          Width = 122
          Height = 15
          Caption = 'High Speed Share Max:'
        end
        object LblDamperLowSpeedShareMin: TLabel
          Left = 16
          Top = 128
          Width = 116
          Height = 15
          Caption = 'Low Speed Share Min:'
        end
        object EditDamperLowSpeedThreshold: TEdit
          Left = 170
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '50.0'
        end
        object EditDamperAsymmetryThreshold: TEdit
          Left = 170
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '8.0'
        end
        object EditDamperHighSpeedShareMax: TEdit
          Left = 170
          Top = 92
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '25.0'
        end
        object EditDamperLowSpeedShareMin: TEdit
          Left = 170
          Top = 124
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '92.0'
        end
      end
      object GroupSuspension: TGroupBox
        Left = 16
        Top = 208
        Width = 552
        Height = 192
        Caption = 'Suspension Geometry'
        TabOrder = 1
        object LblSuspTravelMax: TLabel
          Left = 16
          Top = 32
          Width = 92
          Height = 15
          Caption = 'Max Travel (mm):'
        end
        object LblRideHeightDiffMax: TLabel
          Left = 16
          Top = 64
          Width = 142
          Height = 15
          Caption = 'Max Ride Height Diff (L/R):'
        end
        object LblSuspVelocityMax: TLabel
          Left = 16
          Top = 96
          Width = 113
          Height = 15
          Caption = 'Max Velocity (mm/s):'
        end
        object LblSuspAvgFrontMax: TLabel
          Left = 16
          Top = 128
          Width = 114
          Height = 15
          Caption = 'Max Avg Front Travel:'
        end
        object LblSuspAvgRearMax: TLabel
          Left = 16
          Top = 160
          Width = 109
          Height = 15
          Caption = 'Max Avg Rear Travel:'
        end
        object EditSuspTravelMax: TEdit
          Left = 170
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '8.0'
        end
        object EditRideHeightDiffMax: TEdit
          Left = 170
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '4.0'
        end
        object EditSuspVelocityMax: TEdit
          Left = 170
          Top = 92
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '5.0'
        end
        object EditSuspAvgFrontMax: TEdit
          Left = 170
          Top = 124
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '50.0'
        end
        object EditSuspAvgRearMax: TEdit
          Left = 170
          Top = 156
          Width = 60
          Height = 23
          TabOrder = 4
          Text = '50.0'
        end
      end
    end
    object TabBrake: TTabSheet
      Caption = 'Brake'
      object GroupBrake: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 384
        Caption = 'Brake Thresholds'
        TabOrder = 0
        object LblBrakeTempDiffMax: TLabel
          Left = 16
          Top = 32
          Width = 112
          Height = 15
          Caption = 'Brake Temp Diff Max:'
        end
        object LblBrakeBiasMax: TLabel
          Left = 16
          Top = 64
          Width = 150
          Height = 15
          Caption = 'Max Allowed Brake Bias Diff:'
        end
        object LblBrakeBiasFrontMin: TLabel
          Left = 16
          Top = 96
          Width = 135
          Height = 15
          Caption = 'Min Avg Front Brake Bias:'
        end
        object LblBrakeTorqueMin: TLabel
          Left = 16
          Top = 128
          Width = 95
          Height = 15
          Caption = 'Brake Torque Min:'
        end
        object LblBrakeChannelMismatchMax: TLabel
          Left = 16
          Top = 160
          Width = 176
          Height = 15
          Caption = 'Front/Rear Temp Swap Detection:'
        end
        object LblBrakeTorqueFrontShareMax: TLabel
          Left = 16
          Top = 192
          Width = 149
          Height = 15
          Caption = 'Front Brake Torque % (Max):'
        end
        object LblBrakeTorqueFrontShareMin: TLabel
          Left = 16
          Top = 224
          Width = 147
          Height = 15
          Caption = 'Front Brake Torque % (Min):'
        end
        object LblBrakeTorqueLrDiffRatio: TLabel
          Left = 16
          Top = 256
          Width = 183
          Height = 15
          Caption = 'Left/Right Torque Imbalance Ratio:'
        end
        object LblBrakeBiasSettingFrontMax: TLabel
          Left = 16
          Top = 288
          Width = 153
          Height = 15
          Caption = 'Brake Bias Setting Front Max:'
        end
        object LblBrakeBiasSettingRearMin: TLabel
          Left = 16
          Top = 320
          Width = 146
          Height = 15
          Caption = 'Brake Bias Setting Rear Min:'
        end
        object EditBrakeTempDiffMax: TEdit
          Left = 218
          Top = 29
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '25.0'
        end
        object EditBrakeBiasMax: TEdit
          Left = 218
          Top = 61
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '18.0'
        end
        object EditBrakeBiasFrontMin: TEdit
          Left = 218
          Top = 93
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '90.0'
        end
        object EditBrakeTorqueMin: TEdit
          Left = 218
          Top = 125
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '1.0'
        end
        object EditBrakeChannelMismatchMax: TEdit
          Left = 218
          Top = 157
          Width = 60
          Height = 23
          TabOrder = 4
          Text = '100.0'
        end
        object EditBrakeTorqueFrontShareMax: TEdit
          Left = 218
          Top = 189
          Width = 60
          Height = 23
          TabOrder = 5
          Text = '65.0'
        end
        object EditBrakeTorqueFrontShareMin: TEdit
          Left = 218
          Top = 221
          Width = 60
          Height = 23
          TabOrder = 6
          Text = '40.0'
        end
        object EditBrakeTorqueLrDiffRatio: TEdit
          Left = 218
          Top = 253
          Width = 60
          Height = 23
          TabOrder = 7
          Text = '0.15'
        end
        object EditBrakeBiasSettingFrontMax: TEdit
          Left = 218
          Top = 285
          Width = 60
          Height = 23
          TabOrder = 8
          Text = '60.0'
        end
        object EditBrakeBiasSettingRearMin: TEdit
          Left = 218
          Top = 317
          Width = 60
          Height = 23
          TabOrder = 9
          Text = '40.0'
        end
      end
    end
  end
  object ButtonSave: TButton
    Left = 384
    Top = 450
    Width = 90
    Height = 37
    Caption = 'Save'
    TabOrder = 1
    OnClick = ButtonSaveClick
  end
  object ButtonCancel: TButton
    Left = 486
    Top = 450
    Width = 90
    Height = 37
    Caption = 'Cancel'
    TabOrder = 2
    OnClick = ButtonCancelClick
  end
end
