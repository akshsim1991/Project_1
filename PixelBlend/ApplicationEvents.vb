Imports System.IO
Imports Microsoft.VisualBasic.ApplicationServices

Namespace My
    ' Application-level events (Project Properties > Application > View Application Events).
    Partial Friend Class MyApplication

        ''' <summary>
        ''' Last line of defence: instead of the app silently disappearing, explain what happened and keep a
        ''' log that can be sent to the developer.
        ''' </summary>
        Private Sub MyApplication_UnhandledException(sender As Object, e As Microsoft.VisualBasic.ApplicationServices.UnhandledExceptionEventArgs) Handles Me.UnhandledException
            Dim logPath = WriteCrashLog(e.Exception)
            Dim message = "Sorry, PixelBlend ran into a problem and has to close." & vbCrLf & vbCrLf & e.Exception.Message
            If logPath IsNot Nothing Then message &= vbCrLf & vbCrLf & "Details were saved to:" & vbCrLf & logPath
            MessageBox.Show(message, "PixelBlend", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Sub

        ''' <summary>Writes the error to %LOCALAPPDATA%\PixelBlend\crash.log. Returns the path, or Nothing if it couldn't be written.</summary>
        Private Shared Function WriteCrashLog(ex As Exception) As String
            Try
                Dim folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PixelBlend")
                Directory.CreateDirectory(folder)
                Dim logPath = Path.Combine(folder, "crash.log")
                File.AppendAllText(logPath, String.Format("==== {0:yyyy-MM-dd HH:mm:ss}  PixelBlend {1}{2}{3}{2}{2}",
                                                          DateTime.Now, Application.Info.Version, vbCrLf, ex))
                Return logPath
            Catch logError As Exception When TypeOf logError Is IOException OrElse TypeOf logError Is UnauthorizedAccessException
                Return Nothing
            End Try
        End Function
    End Class
End Namespace
