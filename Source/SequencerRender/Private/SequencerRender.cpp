// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SequencerRender.h"
#include "SequencerRenderStyle.h"
#include "SequencerRenderCommands.h"
#include "Misc/MessageDialog.h"
#include "Misc/FileHelper.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelSequence/Public/LevelSequence.h"
#include "Toolkits/AssetEditorManager.h"
#include "MovieSceneTools/Public/AutomatedLevelSequenceCapture.h"
#include "MovieSceneCaptureDialogModule.h"
#include "MovieScene/Public/MovieSceneTimeHelpers.h"
#include "MovieSceneCapture/Public/Protocols/ImageSequenceProtocol.h"
#include "Interfaces/IPluginManager.h"
#include "LevelEditor.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"


static const FName SequencerRenderTabName("SequencerRender");

#define LOCTEXT_NAMESPACE "FSequencerRenderModule"

bool IsRenderingMovie()
{
    IMovieSceneCaptureDialogModule& movieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
    return movieSceneCaptureModule.GetCurrentCapture().IsValid();
}

void FSequencerRenderModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FSequencerRenderStyle::Initialize();
	FSequencerRenderStyle::ReloadTextures();

	FSequencerRenderCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FSequencerRenderCommands::Get().RenderActiveSequencer,
		FExecuteAction::CreateRaw(this, &FSequencerRenderModule::PluginButtonClicked),
		FCanExecuteAction());
		
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FSequencerRenderModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FSequencerRenderModule::AddToolbarExtension));
		
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
}

void FSequencerRenderModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FSequencerRenderStyle::Shutdown();

	FSequencerRenderCommands::Unregister();
}

void FSequencerRenderModule::PluginButtonClicked()
{
    if (IsRenderingMovie())
    {
        return;
    }

    // find opened sequencer
    bool result = false;
    FString openedSequenceAssetPath;
    TArray <UObject*> Assets = FAssetEditorManager::Get().GetAllEditedAssets();
    for (UObject* Asset : Assets)
    {
        ULevelSequence* OpenedSequencer = Cast<ULevelSequence>(Asset);
        if (OpenedSequencer)
        {
            if (Asset->IsAsset())
            {
                FAssetData ass = FAssetData(Asset);
                openedSequenceAssetPath = ass.GetExportTextName();
                openedSequenceAssetPath = openedSequenceAssetPath.RightChop(13);
                result = true;
                break;
            }
        }
    }

    if (!result)
    {
        return;
    }

    IMovieSceneCaptureDialogModule& MovieSceneCaptureModule = FModuleManager::Get().LoadModuleChecked<IMovieSceneCaptureDialogModule>("MovieSceneCaptureDialog");
    UAutomatedLevelSequenceCapture* movieSceneCapture = NewObject<UAutomatedLevelSequenceCapture>(GetTransientPackage(), UAutomatedLevelSequenceCapture::StaticClass(), UMovieSceneCapture::MovieSceneCaptureUIName, RF_Transient);

    FString outFileName;
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        bool fileSelected = false;
        const FString FileTypes = TEXT("mp4 video (*.mp4)|*.mp4");
        TArray<FString> outFileNames;
        fileSelected = DesktopPlatform->SaveFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("Where to save output video file?"),
            movieSceneCapture->Settings.OutputDirectory.Path,
            TEXT("image.mp4"),
            FileTypes, EFileDialogFlags::None, outFileNames);

        if (fileSelected)
        {
            FString absOutFileName = outFileNames[0];
            absOutFileName = FPaths::SetExtension(absOutFileName, TEXT("mp4"));
            FDirectoryPath dirPath;
            dirPath.Path = FPaths::GetPath(absOutFileName);
            FString fileName = FPaths::GetCleanFilename(absOutFileName);
            if (movieSceneCapture)
            {
                movieSceneCapture->LevelSequenceAsset = FSoftObjectPath(openedSequenceAssetPath);
                movieSceneCapture->Settings.OutputDirectory = dirPath;
                movieSceneCapture->Settings.bOverwriteExisting = true;
                movieSceneCapture->Settings.OutputFormat = TEXT("image.{frame}");
                movieSceneCapture->SetImageCaptureProtocolType(UImageSequenceProtocol_PNG::StaticClass());

                ULevelSequence* LevelSequence = Cast<ULevelSequence>(movieSceneCapture->LevelSequenceAsset.TryLoad());

                if (!movieSceneCapture->bUseCustomStartFrame && !movieSceneCapture->bUseCustomEndFrame)
                {
                    if (!LevelSequence)
                    {
                        const FString ErrorMessage = FString::Printf(TEXT("Specified Level Sequence Asset failed to load. Specified Asset Path: %s"), *movieSceneCapture->LevelSequenceAsset.GetAssetPathString());
                        FFrame::KismetExecutionMessage(*ErrorMessage, ELogVerbosity::Error);
                        return;
                    }

                    FFrameRate DisplayRate = LevelSequence->GetMovieScene()->GetDisplayRate();
                    FFrameRate TickResolution = LevelSequence->GetMovieScene()->GetTickResolution();

                    movieSceneCapture->Settings.FrameRate = DisplayRate;
                    movieSceneCapture->Settings.bUseRelativeFrameNumbers = false;
                    TRange<FFrameNumber> Range = LevelSequence->GetMovieScene()->GetPlaybackRange();

                    FFrameNumber StartFrame = MovieScene::DiscreteInclusiveLower(Range);
                    FFrameNumber EndFrame = MovieScene::DiscreteExclusiveUpper(Range);

                    FFrameNumber RoundedStartFrame = FFrameRate::TransformTime(StartFrame, TickResolution, DisplayRate).CeilToFrame();
                    FFrameNumber RoundedEndFrame = FFrameRate::TransformTime(EndFrame, TickResolution, DisplayRate).CeilToFrame();

                    movieSceneCapture->CustomStartFrame = RoundedStartFrame;
                    movieSceneCapture->CustomEndFrame = RoundedEndFrame;

                    startFrame = movieSceneCapture->CustomStartFrame.Value;
                }

                if (movieSceneCapture->bUseCustomStartFrame)
                {
                    startFrame = movieSceneCapture->CustomStartFrame.Value;
                }

                captureOutputDirectory = movieSceneCapture->Settings.OutputDirectory.Path;

                // prompt confirmation dialog
                if (LevelSequence)
                    captureFps = LevelSequence->GetMovieScene()->GetDisplayRate().AsDecimal();

                const FText confirmOperationMessage = FText::FromString(FString::Printf(TEXT("Sequence will be rendered with folowing settings:\n\n FPS: %f\n Resolution: %dx%d\nStart frame: %d"),
                    captureFps,
                    movieSceneCapture->Settings.Resolution.ResX,
                    movieSceneCapture->Settings.Resolution.ResY,
                    startFrame
                ));

                EAppReturnType::Type confirmed = FMessageDialog::Open(EAppMsgType::OkCancel, confirmOperationMessage);
                if (confirmed == EAppReturnType::Ok)
                {
                    auto onCaptureFinished = [=](bool result)
                    {
                        FString pluginDir = IPluginManager::Get().FindPlugin("SequencerRender")->GetBaseDir();
                        FString ffmpegExe = TEXT("ffmpeg.exe");

                        // Try use args from env variable
                        FString ffmpegArgs = FWindowsPlatformMisc::GetEnvironmentVariable(TEXT("SEQUENCE2MP4ARGS"));
                        FString ffmpegCommand;
                        if (!FPaths::FileExists(ffmpegArgs))
                        {
                            // use default args
                            ffmpegCommand = FString::Printf(TEXT("-start_number %d -framerate %f -i image.%%04d.png -vcodec mpeg4 -pix_fmt yuv420p -q:v 1 -y %s"), startFrame, captureFps, *fileName);
                            UE_LOG(LogTemp, Warning, TEXT("ffmpeg arguments file not found. Using defaults!\n%s"), *ffmpegCommand);
                        }
                        else
                        {
                            FFileHelper::LoadFileToString(ffmpegCommand, *ffmpegArgs);
                            ffmpegCommand = ffmpegCommand.Replace(TEXT("@START_FRAME"), *FString::FromInt(startFrame));
                            ffmpegCommand = ffmpegCommand.Replace(TEXT("@CAPTURE_FPS"), *FString::FromInt(captureFps));
                            ffmpegCommand = ffmpegCommand.Replace(TEXT("@OUT_FILE_NAME"), *fileName);
                        }

                        void* PipeRead = nullptr;
                        void* PipeWrite = nullptr;
                        FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

                        int32 ReturnCode = -1;
                        FString output;
                        FProcHandle pHandle = FPlatformProcess::CreateProc(*ffmpegExe, *ffmpegCommand, false, true, true, nullptr, 0, *captureOutputDirectory, PipeWrite);
                        UE_LOG(LogTemp, Warning, TEXT("Executing command: ffmpeg.exe %s"), *ffmpegCommand);
                        if (pHandle.IsValid())
                        {
                            while (FPlatformProcess::IsProcRunning(pHandle))
                            {
                                output += FPlatformProcess::ReadPipe(PipeRead);
                                FPlatformProcess::Sleep(0.1f);
                            }
                            output += FPlatformProcess::ReadPipe(PipeRead);
                            FPlatformProcess::GetProcReturnCode(pHandle, &ReturnCode);

                            if (ReturnCode != 0)
                            {
                                UE_LOG(LogTemp, Error, TEXT("Error converting image sequece:\n%s"), *output);
                            }

                            const FText cleanupMsg = FText::FromString(TEXT("Remove image sequence?"));
                            EAppReturnType::Type answer = FMessageDialog::Open(EAppMsgType::YesNo, cleanupMsg);
                            if (answer == EAppReturnType::Yes)
                            {
                                // cleanup
                                TArray<FString> foundPngs;
                                IFileManager::Get().FindFiles(foundPngs, *captureOutputDirectory, TEXT("png"));
                                for (FString pngImageFileName : foundPngs)
                                {
                                    FString pngAbsPath = FPaths::Combine(captureOutputDirectory, pngImageFileName);
                                    IFileManager::Get().Delete(*pngAbsPath);
                                }
                            }

                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("Failed to launch ffmpeg.exe!"));
                        }
                        FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
                    };

                    MovieSceneCaptureModule.StartCapture(movieSceneCapture);
                    MovieSceneCaptureModule.GetCurrentCapture()->CaptureStoppedDelegate.AddLambda(onCaptureFinished);
                }

            }
        }
    }

}

void FSequencerRenderModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FSequencerRenderCommands::Get().RenderActiveSequencer);
}

void FSequencerRenderModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FSequencerRenderCommands::Get().RenderActiveSequencer);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSequencerRenderModule, SequencerRender)