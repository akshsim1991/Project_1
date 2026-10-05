Imports System.IO

''' <summary>User preferences remembered between sessions (%APPDATA%\PneuSim\settings.ini).</summary>
Public Module AppSettings
    Public Property DarkMode As Boolean
    Public Property RealPhysics As Boolean
    ''' <summary>Titles of the lessons the user has passed.</summary>
    Public ReadOnly Property CompletedLessons As New HashSet(Of String)

    Private ReadOnly Property FilePath As String
        Get
            Return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "PneuSim", "settings.ini")
        End Get
    End Property

    Public Sub Load()
        Try
            If Not File.Exists(FilePath) Then Return
            For Each line In File.ReadAllLines(FilePath)
                Dim eq = line.IndexOf("="c)
                If eq <= 0 Then Continue For
                Dim key = line.Substring(0, eq).Trim(), value = line.Substring(eq + 1).Trim()
                Select Case key
                    Case "DarkMode" : DarkMode = value = "1"
                    Case "RealPhysics" : RealPhysics = value = "1"
                    Case "Lesson" : If value <> "" Then CompletedLessons.Add(value)
                End Select
            Next
        Catch ex As Exception
            ' Preferences are optional.
        End Try
    End Sub

    Public Sub Save()
        Try
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath))
            Dim lines As New List(Of String) From {$"DarkMode={If(DarkMode, 1, 0)}", $"RealPhysics={If(RealPhysics, 1, 0)}"}
            lines.AddRange(CompletedLessons.Select(Function(l) "Lesson=" & l))
            File.WriteAllLines(FilePath, lines)
        Catch ex As Exception
            ' Preferences are optional.
        End Try
    End Sub
End Module
