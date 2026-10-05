Imports System.IO

''' <summary>User preferences remembered between sessions (%APPDATA%\PneuSim\settings.ini).</summary>
Public Module AppSettings
    Public Property DarkMode As Boolean
    Public Property RealPhysics As Boolean

    Private ReadOnly Property FilePath As String
        Get
            Return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PneuSim", "settings.ini")
        End Get
    End Property

    Public Sub Load()
        Try
            If Not File.Exists(FilePath) Then Return
            For Each line In File.ReadAllLines(FilePath)
                Dim kv = line.Split("="c)
                If kv.Length <> 2 Then Continue For
                Dim on_ = kv(1).Trim() = "1"
                Select Case kv(0).Trim()
                    Case "DarkMode" : DarkMode = on_
                    Case "RealPhysics" : RealPhysics = on_
                End Select
            Next
        Catch ex As Exception
            ' Preferences are optional.
        End Try
    End Sub

    Public Sub Save()
        Try
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath))
            File.WriteAllLines(FilePath, {$"DarkMode={If(DarkMode, 1, 0)}", $"RealPhysics={If(RealPhysics, 1, 0)}"})
        Catch ex As Exception
            ' Preferences are optional.
        End Try
    End Sub
End Module
