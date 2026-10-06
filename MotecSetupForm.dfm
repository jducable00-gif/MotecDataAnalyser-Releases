object FormMotecSetup: TFormMotecSetup
  Left = 0
  Top = 0
  Caption = 'AC EVO  - MoTeC Setup Analyzer'
  ClientHeight = 1173
  ClientWidth = 1471
  Color = clWhite
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'Segoe UI'
  Font.Style = []
  Position = poScreenCenter
  OnResize = FormResize
  TextHeight = 15
  object LabelTitle: TLabel
    Left = 20
    Top = 18
    Width = 221
    Height = 30
    Caption = 'MoTeC Setup Analyzer'
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -21
    Font.Name = 'Segoe UI'
    Font.Style = [fsBold]
    ParentFont = False
  end
  object LabelSubtitle: TLabel
    Left = 20
    Top = 50
    Width = 160
    Height = 15
    Caption = 'Analyze MoTeC telemetry data'
    Color = clWhite
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clGray
    Font.Height = -12
    Font.Name = 'Segoe UI'
    Font.Style = []
    ParentColor = False
    ParentFont = False
  end
  object LabelProfile: TLabel
    Left = 20
    Top = 108
    Width = 37
    Height = 15
    Caption = 'Profile:'
  end
  object LabelLanguage: TLabel
    Left = 210
    Top = 108
    Width = 55
    Height = 15
    Caption = 'Language:'
  end
  object TireTempPaintBox: TPaintBox
    Left = 24
    Top = 180
    Width = 280
    Height = 310
    OnPaint = TireTempPaintBoxPaint
  end
  object BrakeTempPaintBox: TPaintBox
    Left = 310
    Top = 180
    Width = 240
    Height = 310
    OnPaint = BrakeTempPaintBoxPaint
  end
  object LiveLed: TShape
    Left = 642
    Top = 109
    Width = 14
    Height = 14
    Brush.Color = clRed
    Shape = stCircle
  end
  object LabelLiveStatus: TLabel
    Left = 662
    Top = 108
    Width = 72
    Height = 15
    Caption = 'Disconnected'
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clGray
    Font.Height = -12
    Font.Name = 'Segoe UI'
    Font.Style = []
    ParentFont = False
  end
  object DamperFLPaintBox: TPaintBox
    Left = 330
    Top = 184
    Width = 391
    Height = 153
    OnPaint = DamperFLPaintBoxPaint
  end
  object DamperFRPaintBox: TPaintBox
    Left = 721
    Top = 184
    Width = 392
    Height = 153
    OnPaint = DamperFRPaintBoxPaint
  end
  object DamperRLPaintBox: TPaintBox
    Left = 330
    Top = 337
    Width = 391
    Height = 153
    OnPaint = DamperRLPaintBoxPaint
  end
  object DamperRRPaintBox: TPaintBox
    Left = 721
    Top = 337
    Width = 392
    Height = 153
    OnPaint = DamperRRPaintBoxPaint
  end
  object AccentLine: TPanel
    Left = 0
    Top = 82
    Width = 1113
    Height = 3
    BevelOuter = bvNone
    Color = 4426560
    TabOrder = 8
  end
  object ButtonLoad: TButton
    Left = 958
    Top = 44
    Width = 120
    Height = 32
    Caption = 'Load MoTeC CSV'
    TabOrder = 0
    Visible = False
    OnClick = ButtonLoadClick
  end
  object PageControlReport: TPageControl
    Left = 36
    Top = 516
    Width = 1069
    Height = 317
    ActivePage = TabSummary
    TabOrder = 1
    object TabSummary: TTabSheet
      Caption = 'Summary'
      object MemoSummary: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabGeneral: TTabSheet
      Caption = 'General'
      object MemoGeneral: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabTyre: TTabSheet
      Caption = 'Tyre'
      object MemoTyre: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabBrake: TTabSheet
      Caption = 'Brake'
      object MemoBrake: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabSuspension: TTabSheet
      Caption = 'Suspension'
      object MemoSuspension: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabEngineCooling: TTabSheet
      Caption = 'Engine & Cooling'
      object MemoEngineCooling: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
    object TabHandling: TTabSheet
      Caption = 'Handling'
      object MemoHandling: TMemo
        Left = 0
        Top = 0
        Width = 1061
        Height = 287
        Align = alClient
        Color = clWhite
        Font.Charset = DEFAULT_CHARSET
        Font.Color = clWindowText
        Font.Height = -13
        Font.Name = 'Consolas'
        Font.Style = []
        ParentFont = False
        ReadOnly = True
        ScrollBars = ssVertical
        TabOrder = 0
      end
    end
  end
  object StatusBar1: TStatusBar
    Left = 0
    Top = 1154
    Width = 1471
    Height = 19
    Panels = <
      item
        Text = 'FADOQ '
        Width = 50
      end
      item
        Text = 'V5.2.1'
        Width = 50
      end>
  end
  object Button1: TButton
    Left = 935
    Top = 104
    Width = 170
    Height = 34
    Caption = 'Import Motec Data'
    TabOrder = 2
    OnClick = Button1Click
  end
  object ButtonSettings: TButton
    Left = 759
    Top = 104
    Width = 170
    Height = 34
    Caption = 'Settings'
    TabOrder = 9
    OnClick = ButtonSettingsClick
  end
  object ProgressBarExport: TProgressBar
    Left = 20
    Top = 144
    Width = 1073
    Height = 18
    TabOrder = 3
  end
  object LanguageCombo: TComboBox
    Left = 282
    Top = 105
    Width = 130
    Height = 23
    Style = csDropDownList
    TabOrder = 4
    OnChange = LanguageComboChange
  end
  object ChannelProfileCombo: TComboBox
    Left = 84
    Top = 105
    Width = 110
    Height = 23
    Style = csDropDownList
    TabOrder = 5
    OnChange = ChannelProfileComboChange
  end
  object LiveCaptureCheckBox: TCheckBox
    Left = 548
    Top = 107
    Width = 120
    Height = 19
    Caption = 'Live Capture'
    TabOrder = 6
    OnClick = LiveCaptureCheckBoxClick
  end
  object LiveTimer: TTimer
    Enabled = False
    Interval = 16
    OnTimer = LiveTimerTimer
  end
  object LiveConnectTimer: TTimer
    Enabled = False
    OnTimer = LiveConnectTimerTimer
  end
end
