Imports System.IO
Imports System.Text.Json
Imports System.Text.Json.Serialization

''' <summary>One traced curve. Points are stored as image pixel positions so re-calibrating updates every value.</summary>
Public Class DataSeries
    Public Property Name As String = "Series 1"
    ''' <summary>Display color as a 32-bit ARGB value.</summary>
    Public Property ColorArgb As Integer = &HFFD62728
    Public Property Points As New List(Of PointD)

    Public Function Clone() As DataSeries
        Return New DataSeries With {.Name = Name, .ColorArgb = ColorArgb, .Points = New List(Of PointD)(Points)}
    End Function

    ''' <summary>The points converted to graph values (in the order they were traced).</summary>
    Public Function DataPoints(calibration As Calibration) As List(Of PointD)
        Return Points.Select(Function(p) calibration.PixelToData(p)).ToList()
    End Function
End Class

''' <summary>Everything the user works on: the base image, its calibration and the traced curves. Saved as one file.</summary>
Public Class GraphProject
    Public Const FileExtension As String = ".graphproj"
    Public Const CurrentVersion As Integer = 2

    ''' <summary>Distinct colors for new series (colour-blind friendly order).</summary>
    Public Shared ReadOnly SeriesPalette As Integer() = {
        &HFFD62728, &HFF1F77B4, &HFF2CA02C, &HFFFF7F0E, &HFF9467BD, &HFF8C564B, &HFFE377C2, &HFF17BECF}

    Public Property Version As Integer = CurrentVersion
    ''' <summary>Name of the original image file, for display only.</summary>
    Public Property ImageFileName As String
    ''' <summary>The image itself, embedded so the project still opens if the original file is moved.</summary>
    Public Property ImageData As Byte()
    Public Property Calibration As New Calibration
    Public Property Series As New List(Of DataSeries)

    Private Shared ReadOnly JsonOptions As New JsonSerializerOptions With {
        .WriteIndented = True,
        .DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    }

    Public Sub Save(filePath As String)
        ' Write to a temporary file first so a failure never leaves a half-written project behind.
        Dim tempPath = filePath & ".tmp"
        File.WriteAllText(tempPath, JsonSerializer.Serialize(Me, JsonOptions))
        File.Move(tempPath, filePath, overwrite:=True)
    End Sub

    Public Shared Function Load(filePath As String) As GraphProject
        Dim project As GraphProject
        Try
            project = JsonSerializer.Deserialize(Of GraphProject)(File.ReadAllText(filePath), JsonOptions)
        Catch ex As JsonException
            Throw New InvalidDataException("This is not a Graphing project file, or it is damaged.", ex)
        End Try
        If project Is Nothing Then Throw New InvalidDataException("This is not a Graphing project file.")
        If project.Version > CurrentVersion Then
            Throw New InvalidDataException("This project was saved by a newer version of Graphing. Please update Graphing to open it.")
        End If
        project.Calibration = If(project.Calibration, New Calibration)
        project.Series = If(project.Series, New List(Of DataSeries))
        For Each s In project.Series
            s.Points = If(s.Points, New List(Of PointD))
        Next
        Return project
    End Function

    ''' <summary>A new series with the next unused name and color.</summary>
    Public Function CreateSeries() As DataSeries
        Dim number = Series.Count + 1
        While Series.Any(Function(s) s.Name = "Series " & number)
            number += 1
        End While
        Return New DataSeries With {.Name = "Series " & number, .ColorArgb = SeriesPalette((number - 1) Mod SeriesPalette.Length)}
    End Function

    ''' <summary>Captures the editable state (calibration and series, not the image) for undo.</summary>
    Public Function CaptureState() As EditState
        Return New EditState(Calibration.Clone(), Series.Select(Function(s) s.Clone()).ToList())
    End Function

    Public Sub RestoreState(state As EditState)
        Calibration = state.Calibration.Clone()
        Series = state.Series.Select(Function(s) s.Clone()).ToList()
    End Sub
End Class

''' <summary>A snapshot of everything undo/redo can change.</summary>
Public NotInheritable Class EditState
    Public ReadOnly Property Calibration As Calibration
    Public ReadOnly Property Series As IReadOnlyList(Of DataSeries)

    Public Sub New(calibration As Calibration, series As IReadOnlyList(Of DataSeries))
        Me.Calibration = calibration
        Me.Series = series
    End Sub
End Class

''' <summary>Undo/redo history made of snapshots taken just before each change.</summary>
Public Class UndoHistory
    Private Const Limit As Integer = 200
    Private ReadOnly _undo As New LinkedList(Of EditState)
    Private ReadOnly _redo As New Stack(Of EditState)

    Public ReadOnly Property CanUndo As Boolean
        Get
            Return _undo.Count > 0
        End Get
    End Property

    Public ReadOnly Property CanRedo As Boolean
        Get
            Return _redo.Count > 0
        End Get
    End Property

    ''' <summary>Call just before changing the project, with its current state.</summary>
    Public Sub Record(stateBeforeChange As EditState)
        _undo.AddLast(stateBeforeChange)
        If _undo.Count > Limit Then _undo.RemoveFirst()
        _redo.Clear()
    End Sub

    ''' <summary>Returns the state to go back to, given the current one, or Nothing.</summary>
    Public Function Undo(current As EditState) As EditState
        If _undo.Count = 0 Then Return Nothing
        Dim previous = _undo.Last.Value
        _undo.RemoveLast()
        _redo.Push(current)
        Return previous
    End Function

    Public Function Redo(current As EditState) As EditState
        If _redo.Count = 0 Then Return Nothing
        _undo.AddLast(current)
        Return _redo.Pop()
    End Function

    Public Sub Clear()
        _undo.Clear()
        _redo.Clear()
    End Sub
End Class
