Imports System.Reflection

''' <summary>
''' Records the state of a running simulation a few times per second so it can be rewound,
''' stepped through and resumed from any recorded moment. Every value field of the simulator,
''' the components and their ports is copied (lists such as the plotter data are cut back to the
''' restored time instead).
''' </summary>
Public Class SimulationRecorder

    ''' <summary>Seconds of simulated time between frames.</summary>
    Public Const Interval As Double = 0.05
    ''' <summary>Frames kept (the oldest are dropped): 2 minutes.</summary>
    Public Const MaxFrames As Integer = 2400

    Private Class Frame
        Public Time As Double
        Public Signature As String
        Public Values As New List(Of Object())
    End Class

    Private Shared ReadOnly FieldCache As New Dictionary(Of Type, FieldInfo())
    Private ReadOnly _sim As Simulator
    Private ReadOnly _frames As New List(Of Frame)
    Private _index As Integer = -1

    Public Sub New(sim As Simulator)
        _sim = sim
    End Sub

    Public ReadOnly Property Count As Integer
        Get
            Return _frames.Count
        End Get
    End Property

    ''' <summary>Index of the frame shown now (the last one while running).</summary>
    Public ReadOnly Property Index As Integer
        Get
            Return _index
        End Get
    End Property

    ''' <summary>True when an older frame is shown (rewound).</summary>
    Public ReadOnly Property IsRewound As Boolean
        Get
            Return _index >= 0 AndAlso _index < _frames.Count - 1
        End Get
    End Property

    Public Function TimeAt(i As Integer) As Double
        Return _frames(i).Time
    End Function

    ''' <summary>Value fields of a type and its base types (cached).</summary>
    Private Shared Function FieldsOf(t As Type) As FieldInfo()
        Dim result As FieldInfo() = Nothing
        SyncLock FieldCache
            If FieldCache.TryGetValue(t, result) Then Return result
            Dim list As New List(Of FieldInfo)
            Dim cur = t
            While cur IsNot Nothing AndAlso cur IsNot GetType(Object)
                For Each f In cur.GetFields(BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.DeclaredOnly)
                    If f.IsInitOnly Then Continue For
                    If f.FieldType.IsValueType OrElse f.FieldType Is GetType(String) Then list.Add(f)
                Next
                cur = cur.BaseType
            End While
            result = list.ToArray()
            FieldCache(t) = result
        End SyncLock
        Return result
    End Function

    Private Shared Function Capture(o As Object) As Object()
        Dim fields = FieldsOf(o.GetType())
        Dim values(fields.Length - 1) As Object
        For i = 0 To fields.Length - 1
            values(i) = fields(i).GetValue(o)
        Next
        Return values
    End Function

    Private Shared Sub Apply(o As Object, values As Object())
        Dim fields = FieldsOf(o.GetType())
        For i = 0 To fields.Length - 1
            fields(i).SetValue(o, values(i))
        Next
    End Sub

    ''' <summary>Objects whose state is recorded, always in the same order.</summary>
    Private Function Objects() As IEnumerable(Of Object)
        Dim list As New List(Of Object) From {_sim}
        For Each e In _sim.Circuit.Elements
            list.Add(e)
            list.AddRange(e.Ports)
        Next
        Return list
    End Function

    ''' <summary>Records the present moment if a frame is due (or always with <paramref name="force"/>).</summary>
    Public Sub Record(Optional force As Boolean = False)
        If IsRewound Then TruncateAfterCurrent()
        If Not force AndAlso _frames.Count > 0 AndAlso _sim.Time < _frames(_frames.Count - 1).Time + Interval - 0.000001 Then Return
        Dim f As New Frame With {.Time = _sim.Time, .Signature = _sim.DiscreteState()}
        For Each o In Objects()
            f.Values.Add(Capture(o))
        Next
        _frames.Add(f)
        If _frames.Count > MaxFrames Then _frames.RemoveAt(0)
        _index = _frames.Count - 1
    End Sub

    ''' <summary>Goes back (or forward) to a recorded frame.</summary>
    Public Sub Restore(i As Integer)
        If _frames.Count = 0 Then Return
        i = Math.Max(0, Math.Min(_frames.Count - 1, i))
        Dim f = _frames(i)
        Dim objs = Objects().ToList()
        For k = 0 To Math.Min(objs.Count, f.Values.Count) - 1
            Apply(objs(k), f.Values(k))
        Next
        For Each e In _sim.Circuit.Elements
            e.AfterStateRestored()
        Next
        _sim.TrimRecordings(f.Time)
        _index = i
    End Sub

    ''' <summary>Forgets the frames after the one shown (the run continues from here on a new path).</summary>
    Public Sub TruncateAfterCurrent()
        If _index >= 0 AndAlso _index < _frames.Count - 1 Then _frames.RemoveRange(_index + 1, _frames.Count - _index - 1)
    End Sub

    ''' <summary>The next recorded frame after the current one where a valve, relay, contact or cylinder end position changed, or -1.</summary>
    Public Function NextEventFrame() As Integer
        If _index < 0 Then Return -1
        Dim sig = _frames(_index).Signature
        For i = _index + 1 To _frames.Count - 1
            If _frames(i).Signature <> sig Then Return i
        Next
        Return -1
    End Function

    ''' <summary>The previous frame before which something switched, or -1.</summary>
    Public Function PreviousEventFrame() As Integer
        If _index <= 0 Then Return -1
        Dim sig = _frames(_index).Signature
        For i = _index - 1 To 0 Step -1
            If _frames(i).Signature <> sig Then Return i
        Next
        Return -1
    End Function
End Class
