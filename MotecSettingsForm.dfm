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

        object EditMinDamperSamples: TEdit
          Left = 212
          Top = 30
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '20'
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

      object GroupDamperZones: TGroupBox
        Left = 16
        Top = 16
        Width = 552
        Height = 230
        Caption = 'Damper 4-Zone Targets (%)'
        TabOrder = 0
        object LblHscTargetMin: TLabel
          Left = 16
          Top = 32
          Width = 100
          Height = 15
          Caption = 'Hi Comp Min:'
        end
        object LblHscTargetMax: TLabel
          Left = 300
          Top = 32
          Width = 100
          Height = 15
          Caption = 'Hi Comp Max:'
        end
        object LblLscTargetMin: TLabel
          Left = 16
          Top = 64
          Width = 100
          Height = 15
          Caption = 'Lo Comp Min:'
        end
        object LblLscTargetMax: TLabel
          Left = 300
          Top = 64
          Width = 100
          Height = 15
          Caption = 'Lo Comp Max:'
        end
        object LblLsrTargetMin: TLabel
          Left = 16
          Top = 96
          Width = 100
          Height = 15
          Caption = 'Lo Rebound Min:'
        end
        object LblLsrTargetMax: TLabel
          Left = 300
          Top = 96
          Width = 100
          Height = 15
          Caption = 'Lo Rebound Max:'
        end
        object LblHsrTargetMin: TLabel
          Left = 16
          Top = 128
          Width = 100
          Height = 15
          Caption = 'Hi Rebound Min:'
        end
        object LblHsrTargetMax: TLabel
          Left = 300
          Top = 128
          Width = 100
          Height = 15
          Caption = 'Hi Rebound Max:'
        end
        object LblZoneModerateBand: TLabel
          Left = 16
          Top = 160
          Width = 110
          Height = 15
          Caption = 'Moderate Band (pts):'
        end
        object LblDamperSlowSpeedRange: TLabel
          Left = 300
          Top = 160
          Width = 140
          Height = 15
          Caption = 'Slow Speed Range (mm/s):'
        end
        object EditHscTargetMin: TEdit
          Left = 130
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 0
          Text = '10.0'
        end
        object EditHscTargetMax: TEdit
          Left = 420
          Top = 28
          Width = 60
          Height = 23
          TabOrder = 1
          Text = '20.0'
        end
        object EditLscTargetMin: TEdit
          Left = 130
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 2
          Text = '30.0'
        end
        object EditLscTargetMax: TEdit
          Left = 420
          Top = 60
          Width = 60
          Height = 23
          TabOrder = 3
          Text = '40.0'
        end
        object EditLsrTargetMin: TEdit
          Left = 130
          Top = 92
          Width = 60
          Height = 23
          TabOrder = 4
          Text = '30.0'
        end
        object EditLsrTargetMax: TEdit
          Left = 420
          Top = 92
          Width = 60
          Height = 23
          TabOrder = 5
          Text = '40.0'
        end
        object EditHsrTargetMin: TEdit
          Left = 130
          Top = 124
          Width = 60
          Height = 23
          TabOrder = 6
          Text = '10.0'
        end
        object EditHsrTargetMax: TEdit
          Left = 420
          Top = 124
          Width = 60
          Height = 23
          TabOrder = 7
          Text = '20.0'
        end
        object EditZoneModerateBand: TEdit
          Left = 130
          Top = 156
          Width = 60
          Height = 23
          TabOrder = 8
          Text = '5.0'
        end
        object EditDamperSlowSpeedRange: TEdit
          Left = 420
          Top = 156
          Width = 60
          Height = 23
          TabOrder = 9
          Text = '25.0'
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
