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
#include <wchar.h>            // wcstok, _snwprintf (parsing de la ligne de commande /updated)
#include <mmsystem.h>          // timeBeginPeriod / timeEndPeriod (1 ms timer resolution)
#include <gdiplus.h>           // GDI+ initialization/shutdown must live here, in _tWinMain

// Self-update : quand l instance courante est relancee depuis le chemin .new.exe avec
// "/updated \"<nom d origine>\\"", on renomme cette exe vers son nom canonique (raccourcis)
// des que possible. Retourne true si un renommage a ete effectue ou n'etait pas necessaire.
static bool SelfUpdateRenameFromNewExe(LPTSTR cmdLine, const String& selfPath)
{
	if (!cmdLine || wcsstr(cmdLine, L"/updated") == NULL) return false;

	const std::wstring selfW(selfPath.c_str());
	static const wchar_t* kSuffix = L".new.exe";   // insensible a la casse (Windows).
	bool isNewExe = selfW.size() > 8 && _wcsnicmp(&selfW[selfW.size()-8], kSuffix, 8) == 0;
	if (!isNewExe) return false;

	// Token suivant "/updated" dans une COPIE de la ligne de commande (wcstok modifie son entree).
	std::wstring origNameW;
	bool afterFlag = false;
	wchar_t buf[1024] = {0}; swprintf_s(buf, 1024, L"%ls", cmdLine);   // copie locale (wcstok modifie son entree).
	for (wchar_t* tok = wcstok(buf, L" \t"); tok != NULL; tok = wcstok(NULL, L" \t")) {
		if (_wcsicmp(tok, L"/updated") == 0) { afterFlag = true; continue; }
		if (!afterFlag) continue;
		std::wstring t(tok);
		if (t.size() >= 2 && ((t.front() == L'"') == (t.back() == L'"'))) t = t.substr(1, t.size()-2);   // dequote si present.
		origNameW.swap(t); break;
	}

	const String dirPath = IncludeTrailingPathDelimiter(ExtractFilePath(selfPath));
	String target = origNameW.empty() ? (dirPath + "Project1.exe") : (dirPath + origNameW.c_str());   // repli canonique si nom absent.
	if (_wcsicmp(target.c_str(), selfPath.c_str()) == 0) return true;                                  // deja au bon endroit.

	return MoveFile(selfPath.c_str(), target.c_str()) != 0;                                            // best-effort : ignore l echec (l app continue).
}
#pragma comment(lib, "winmm")  // winmm.lib for timeBeginPeriod/timeEndPeriod
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
//---------------------------------------------------------------------------
USEFORM("MotecSetupForm.cpp", FormMotecSetup);
USEFORM("MotecSettingsForm.cpp", FormMotecSettings);
#include "MotecSetupForm.h"
#include "MotecSettingsForm.h"
//---------------------------------------------------------------------------
int WINAPI _tWinMain(HINSTANCE, HINSTANCE, LPTSTR cmdLine, int)
{
	// Unlock 1 ms timer resolution so TTimer can fire at sub-16 ms intervals.
	timeBeginPeriod(1);

	// GDI+ MUST be initialized before any form is created — the paint handlers in
	// MotecSetupForm use Gdiplus::Graphics/SolidBrush/Font directly. Without a valid
	// token, every GDI+ call corrupts internal heap state (detected by Embarcadero's
	// debug CRT as exception 0xEEDFADE). Shutdown must happen AFTER Application->Run()
	// returns and all forms are destroyed.
	ULONG_PTR gdiplusToken = 0;
	GdiplusStartupInput gdiplusInput;
	Status gdiStatus = GdiplusStartup(&gdiplusToken, &gdiplusInput, NULL);

	try
	{
		Application->Initialize();
		Application->MainFormOnTaskBar = true;

		// Self-update: when relaunched with /updated the previous exe was renamed to
		// .old.exe. Delete it now that no process holds a lock on it (best-effort).
		if (cmdLine && wcsstr(cmdLine, L"/updated") != NULL) {
			const String oldExe = IncludeTrailingPathDelimiter(ExtractFilePath(Application->ExeName)) + "MotecDataAnalyser.old.exe";
			DeleteFile(oldExe.c_str());   // ignore failure — file may already be gone

			// La nouvelle instance tourne depuis MotecDataAnalyser.new.exe : on la renomme vers
			// son nom d origine (passe en argument) pour que les raccourcis continuent a pointer.
			SelfUpdateRenameFromNewExe(cmdLine, Application->ExeName);   // best-effort ; ignore l echec.
		}

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

	// CRITICAL ORDERING: The main form is owned by TApplication and its C++ destructor
	// fires during Application's static cleanup — which happens AFTER _tWinMain returns.
	// If GdiplusShutdown runs before that, any residual GDI+ state in VCL controls or
	// heap guard-bytes allocated while GDI+ was live will trigger 0xEEDFADE at exit.
	// Fix: explicitly destroy the form NOW (while we're still inside _tWinMain), then
	// shut down GDI+. DoFormClose already did all resource cleanup; this just runs the
	// C++ destructor + implicit member destructors in a safe context.
	if (FormMotecSetup) {
		delete FormMotecSetup;   // triggers ~TFormMotecSetup() NOW, before GdiplusShutdown
		FormMotecSetup = nullptr;
	}

	if (gdiplusToken != 0) {
		GdiplusShutdown(gdiplusToken);
		gdiplusToken = 0;
	}

	timeEndPeriod(1);
	return 0;
}
//---------------------------------------------------------------------------
