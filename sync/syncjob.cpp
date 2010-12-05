/////////////////////////////////////////////////////////////////////////////////
// Author:      Steven Lamerton
// Copyright:   Copyright (C) 2009 - 2010 Steven Lamerton
// License:     GNU GPL 2 http://www.gnu.org/licenses/gpl-2.0.html
/////////////////////////////////////////////////////////////////////////////////

#include "syncjob.h"
#include "../rules.h"
#include "../toucan.h"
#include "../data/syncdata.h"
#include "../basicfunctions.h"
#include "../fileops.h"
#include "../path.h"

#include <list>
#include <map>
#include <wx/string.h>
#include <wx/log.h>
#include <wx/dir.h>
#include <wx/filename.h>

SyncJob::SyncJob(SyncData *Data) : Job(Data){
	;
}

void* SyncJob::Entry(){
	SyncData *data = static_cast<SyncData*>(GetData());
	SyncFiles sync(data->GetSource(), data->GetDest(), data);
	sync.Execute();
	return NULL;
}

SyncFiles::SyncFiles(const wxString &syncsource, const wxString &syncdest, SyncData* syncdata) 
          : SyncBase(syncsource, syncdest, syncdata)
{
    Path::Create(sourceroot);
    Path::Create(destroot);
}

bool SyncFiles::Execute(){
	std::list<wxString> sourcepaths = FolderContentsToList(sourceroot);
	std::list<wxString> destpaths = FolderContentsToList(destroot);
	std::map<wxString, int> mergeresult = MergeListsToMap(sourcepaths, destpaths);
	OperationCaller(mergeresult);
	return true;
}

void SyncFiles::OperationCaller(std::map<wxString, int> paths){
	for(std::map<wxString, int>::iterator iter = paths.begin(); iter != paths.end(); ++iter){
		if(wxGetApp().GetAbort()){
			return;
		}
		if(wxDirExists(sourceroot + wxFILE_SEP_PATH + (*iter).first) || wxDirExists(destroot + wxFILE_SEP_PATH + (*iter).first)){
			if((*iter).second == 1){
				OnSourceNotDestFolder((*iter).first);
			}
			else if((*iter).second == 2){
				OnNotSourceDestFolder((*iter).first);				
			}
			else if((*iter).second == 3){
				OnSourceAndDestFolder((*iter).first);
			}
		}
		//We have a file
		else{
			if((*iter).second == 1){
				OnSourceNotDestFile((*iter).first);
			}
			else if((*iter).second == 2){
				OnNotSourceDestFile((*iter).first);				
			}
			else if((*iter).second == 3){
				OnSourceAndDestFile((*iter).first);
			}
			//Update the progress bar for files only
			if(wxGetApp().IsGui()){
				IncrementGauge();
				//If we have a file in both folders then increment again as we only do one pass
				if((*iter).second == 3){
					//But only if we are in a two way sync
					if(data->GetFunction() == _("Mirror") || data->GetFunction() == _("Equalise")){
						IncrementGauge();	
					}
				}
			}
		}
	}
	return;
}

void SyncFiles::OnSourceNotDestFile(const wxString &path){
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	//Clean doesnt copy any files
	if(data->GetFunction() != _("Clean")){
		if(data->GetRules()->Matches(wxFileName::FileName(source)) != Excluded){
			if(CopyIfNeeded(source, dest)){
				if(data->GetFunction() == _("Move")){
					RemoveFile(source);
				}
			}	
		}	
	}
}

void SyncFiles::OnNotSourceDestFile(const wxString &path){
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	if(data->GetFunction() == _("Mirror") || data->GetFunction() == _("Clean")){
		if(data->GetRules()->Matches(wxFileName::FileName(dest)) != Excluded){
			RemoveFile(dest);	
		}
	}
	else if(data->GetFunction() == _("Equalise")){
		if(data->GetRules()->Matches(wxFileName::FileName(dest)) != Excluded){
			CopyIfNeeded(dest, source);
		}
	}
}

void SyncFiles::OnSourceAndDestFile(const wxString &path){
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	if(data->GetFunction() == _("Copy") || data->GetFunction() == _("Mirror") || data->GetFunction() == _("Move")){
		if(data->GetRules()->Matches(wxFileName::FileName(source)) != Excluded){
			if(CopyIfNeeded(source, dest)){
				if(data->GetFunction() == _("Move")){
					RemoveFile(source);
				}				
			}
		}
	}	
	else if(data->GetFunction() == _("Equalise")){
		SourceAndDestCopy(source, dest);
	}
}

void SyncFiles::OnSourceNotDestFolder(const wxString &path){
	//Call the function on the next subfolder
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	//Always recurse into the next directory
	SyncFiles sync(source, dest, data);
	sync.Execute();
	if(data->GetFunction() != _("Clean")){
		wxDir destdir(dest);
		wxDir sourcedir(source);
		if(!destdir.HasFiles() && !destdir.HasSubDirs() && data->GetRules()->Matches(wxFileName::DirName(source)) != Excluded){
			wxRmdir(dest);
		}
		else{
			//Set the timestamps if needed
			if(data->GetTimeStamps()){
				CopyFolderTimestamp(source, dest);
			}	
		}
		if(!sourcedir.HasFiles() && !sourcedir.HasSubDirs() && data->GetFunction() == _("Move")){
			//If we are moving and there are no files left then we need to remove the folder
			wxRmdir(source);
		}
	}
}

void SyncFiles::OnNotSourceDestFolder(const wxString &path){
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	if(data->GetFunction() == _("Mirror") || data->GetFunction() == _("Clean")){
		if(data->GetRules()->Matches(wxFileName::FileName(dest)) != Excluded){
			RemoveDirectory(dest);		
		}
	}
	else if(data->GetFunction() == _("Equalise")){
		SyncFiles sync(source, dest, data);
		sync.Execute();
		wxDir dir(source);
		if(!dir.HasFiles() && !dir.HasSubDirs() && data->GetRules()->Matches(wxFileName::DirName(dest)) != Excluded){
			wxRmdir(dest);
		}
		else{
			//Set the timestamps if needed
			if(data->GetTimeStamps()){
				CopyFolderTimestamp(dest, source);
			}	
		}
	}
}

void SyncFiles::OnSourceAndDestFolder(const wxString &path){
	wxString source = sourceroot + wxFILE_SEP_PATH + path;
	wxString dest = destroot + wxFILE_SEP_PATH + path;
	//Always recurse into the next directory
	SyncFiles sync(source, dest, data);
	sync.Execute();
	if(data->GetFunction() != _("Clean")){
		wxDir destdir(dest);
		wxDir sourcedir(source);
		if(!destdir.HasFiles() && !destdir.HasSubDirs() && data->GetRules()->Matches(wxFileName::DirName(source)) != Excluded){
			wxRmdir(dest);
		}
		else{
			//Set the timestamps if needed
			if(data->GetTimeStamps()){
				CopyFolderTimestamp(source, dest);
			}	
		}
		if(!sourcedir.HasFiles() && !sourcedir.HasSubDirs() && data->GetFunction() == _("Move")){
			//If we are moving and there are no files left then we need to remove the folder
			wxRmdir(source);
		}
	}
}

bool SyncFiles::CopyFile(const wxString &source, const wxString &dest){
	//ATTN : Needs linux support
	#ifdef __WXMSW__
		long destAttributes = 0;
		long sourceAttributes = 0;
		if(data->GetIgnoreRO() || data->GetAttributes()){
				destAttributes = wxFileExists(dest) ? GetFileAttributes(dest) : FILE_ATTRIBUTE_NORMAL;
				sourceAttributes = wxFileExists(source) ? GetFileAttributes(source) : FILE_ATTRIBUTE_NORMAL;
		}
		if(data->GetIgnoreRO()){
			SetFileAttributes(dest, FILE_ATTRIBUTE_NORMAL);
		}
	#endif

	wxString desttemp = wxPathOnly(dest) + wxFILE_SEP_PATH + wxT("Toucan.tmp");
	if(File::Copy(source, desttemp)){
		if(File::Rename(desttemp, dest, true)){
			OutputProgress(_("Copied ") + source);
		}
		else{
			OutputProgress(_("Failed to copy ") + source, true, true);
			if(wxFileExists(desttemp)){
				wxRemoveFile(desttemp);
			}
			#ifdef __WXMSW__
				if(data->GetIgnoreRO()){
					SetFileAttributes(source, sourceAttributes); 
				}
			#endif 
			return false;
		}
	}
	else{
		OutputProgress(_("Failed to copy ") + source, true, true);
		#ifdef __WXMSW__
			if(data->GetIgnoreRO()){

				SetFileAttributes(source, sourceAttributes); 
			} 
		#endif
		return false;
	}
	if(data->GetTimeStamps()){
		wxFileName from(source);
		wxFileName to(dest);
		wxDateTime access, mod, created;
		if(from.GetTimes(&access, &mod, &created)){
			to.SetTimes(&access, &mod, &created); 
		}
	}	
	#ifdef __WXMSW__
		if(data->GetIgnoreRO()){
			SetFileAttributes(dest, destAttributes); 
			SetFileAttributes(source, sourceAttributes); 
		} 
		if(data->GetAttributes()){                   
			SetFileAttributes(dest, sourceAttributes);
		}
	#endif
	return true;
}

bool SyncFiles::CopyIfNeeded(wxString source, wxString dest){
	//If the dest file doesn't exists then we must copy
	if(!wxFileExists(dest)){
        return CopyFile(source, dest);
	}
	//If copy if anything says copy
	if(    (data->GetCheckSize() && ShouldCopySize(source, dest))
		|| (data->GetCheckTime() && ShouldCopyTime(source, dest))
		|| (data->GetCheckShort() && ShouldCopyShort(source, dest))
		|| (data->GetCheckFull() && ShouldCopyFull(source, dest))
        || (!data->GetCheckSize() && !data->GetCheckTime() && 
            !data->GetCheckShort() && !data->GetCheckFull())){
		return CopyFile(source, dest);
	}
	return false;
}

bool SyncFiles::RemoveDirectory(wxString path){
	if(wxGetApp().GetAbort()){
		return true;
	}
	//Make sure that the correct ending is appended
	if (path[path.length()-1] != wxFILE_SEP_PATH) {
		path += wxFILE_SEP_PATH;       
	}
	//If we have a root directory then return
	if (path.length() <= 3) {
		return false;
	}
	wxDir* dir = new wxDir(path);
	wxString filename;
	bool blDir = dir->GetFirst(&filename);
	if(blDir){
		do {
			if(wxGetApp().GetAbort()){
				return true;
			}
			if(wxDirExists(path + filename)){
				RemoveDirectory(path + filename);
			}
			else{
				if(RemoveFile(path + filename)){
					//We have to increment the gauge for ourself here
					if(wxGetApp().IsGui()){
						IncrementGauge();					
					}
                }
            }
	
		}
		while (dir->GetNext(&filename) );
	} 
	delete dir;
	{
  		wxLogNull log;
		if(wxFileName::Rmdir(path)){
			OutputProgress(_("Removed ") + path);
		}
	}
	return true;
}

bool SyncFiles::CopyFolderTimestamp(const wxString &source, const wxString &dest){
	wxFileName from = wxFileName::DirName(source);
	wxFileName to = wxFileName::DirName(dest);
	wxDateTime access, mod, created;
	from.GetTimes(&access, &mod, &created);
	to.SetTimes(&access, &mod, &created); 
	return true;
}

bool SyncFiles::RemoveFile(const wxString &path){
	if(File::Delete(path, data->GetRecycle(), data->GetIgnoreRO())){
		OutputProgress(_("Removed ") + path);		
		return true;
	}
	return false;
}

bool SyncFiles::SourceAndDestCopy(const wxString &source, const wxString &dest){
	wxDateTime tmTo, tmFrom;
	wxFileName flTo(dest);
	wxFileName flFrom(source);
	flTo.GetTimes(NULL, &tmTo, NULL);
	flFrom.GetTimes(NULL, &tmFrom, NULL);		

	tmFrom.MakeTimezone(wxDateTime::UTC, true);
	tmTo.MakeTimezone(wxDateTime::UTC, true);

	if(tmFrom.IsLaterThan(tmTo)){
		if(data->GetRules()->Matches(wxFileName::FileName(source)) != Excluded){
			CopyIfNeeded(source, dest);			
		}
	}
	else if(tmTo.IsLaterThan(tmFrom)){
		if(data->GetRules()->Matches(wxFileName::FileName(source)) != Excluded){
			CopyIfNeeded(dest, source);
		}
	}
	return true;	
}
