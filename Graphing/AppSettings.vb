Imports System.IO
Imports System.Text.Json

''' <summary>Preferences remembered between runs, stored in %LOCALAPPDATA%\Graphing\settings.json.</summary>
Public Class AppSettings
    Public Property WindowBounds As Rectangle
    Public Property WindowMaximized As Boolean
    Public Property LastImageFolder As String
    Public Property LastProjectFolder As String
    Public Property LastExportFolder As String
    Public Property ShowMagnifier As Boolean = True
    Public Property ShowGrid As Boolean
    Public Property ShowLines As Boolean = True
    Public Property DragSpacing As Integer = 5
    Public Property ColorTolerance As Integer = 60
    Public Property AutoTraceStep As Integer = 5
    Public Property ResampleCount As Integer = 50

    Private Shared ReadOnly FilePath As String =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Graphing", "settings.json")

    ''' <summary>Loads the saved settings, or defaults if there are none or they can't be read.</summary>
    Public Shared Function Load() As AppSettings
        Try
            If File.Exists(FilePath) Then
                Return If(JsonSerializer.Deserialize(Of AppSettings)(File.ReadAllText(FilePath)), New AppSettings())
            End If
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException OrElse TypeOf ex Is JsonException
            ' Unreadable settings are not worth bothering the user about; start with defaults.
        End Try
        Return New AppSettings()
    End Function

    Public Sub Save()
        Try
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath))
            File.WriteAllText(FilePath, JsonSerializer.Serialize(Me))
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            ' Settings are a convenience; failing to save them must not stop the app from closing.
        End Try
    End Sub
End Class
