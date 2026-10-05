//---------------------------------------------------------------------------
// MotecDataAnalyser — Point d'entrée de l'application (WinMain).
//
// Application VCL Win32 (C++ Builder 12 / Clang). Ce fichier initialise le
// framework VCL, crée la fenêtre principale (TFormMotecSetup) puis démarre la
// boucle d'événements. Toute la logique métier se trouve dans
// MotecSetupForm.cpp (interface) et analyze_motec_setup.cpp (analyse).
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop
#include <tchar.h>
//---------------------------------------------------------------------------
USEFORM("MotecSetupForm.cpp", FormMotecSetup);
USEFORM("MotecSettingsForm.cpp", FormMotecSettings);
//---------------------------------------------------------------------------
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR, int)
{
	try
	{
		Application->Initialize();
		Application->MainFormOnTaskBar = true;
		Application->CreateForm(__classid(TFormMotecSetup), &FormMotecSetup);
		Application->Run();
	}
	catch (Exception &exception)
	{
		Application->ShowException(&exception);
	}
	catch (...)
	{
		try
		{
			throw Exception("");
		}
		catch (Exception &exception)
		{
			Application->ShowException(&exception);
		}
	}
	return 0;
}
//---------------------------------------------------------------------------
