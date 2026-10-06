Imports System.ComponentModel
Imports System.Text

''' <summary>A fault that can be put into a component or a tube for troubleshooting practice.</summary>
<TypeConverter(GetType(EnumDescriptionConverter))>
Public Enum FaultKind
    <Description("No fault")> None
    <Description("Leaking")> Leak
    <Description("Blocked")> Blocked
    <Description("Stuck in the normal position")> StuckNormal
    <Description("Stuck in the operated position")> StuckOperated
    <Description("Coil burnt out")> BurntCoil
    <Description("Contact does not close")> ContactOpen
    <Description("Contact stuck closed")> ContactWelded
    <Description("Sticking (high friction)")> Sticking
    <Description("Jammed")> Jammed
    <Description("Low output")> LowOutput
End Enum

''' <summary>Helpers for faults: names, and the scrambled form used for hidden faults in files.</summary>
Public Module Faults

    ''' <summary>The general description of a fault kind.</summary>
    Public Function Describe(kind As FaultKind) As String
        Dim field = GetType(FaultKind).GetField(kind.ToString())
        Dim attr = CType(Attribute.GetCustomAttribute(field, GetType(DescriptionAttribute)), DescriptionAttribute)
        Return If(attr Is Nothing, kind.ToString(), attr.Description)
    End Function

    ''' <summary>
    ''' Hidden faults are written to files in a scrambled form, so a student cannot simply read
    ''' the answer in the file (this is not meant as real protection).
    ''' </summary>
    Public Function Scramble(kind As FaultKind) As String
        Dim chars = ("ps-" & kind.ToString()).Reverse().ToArray()
        Return Convert.ToBase64String(Encoding.UTF8.GetBytes(chars))
    End Function

    Public Function Unscramble(text As String) As FaultKind
        Try
            Dim plain = New String(Encoding.UTF8.GetString(Convert.FromBase64String(text)).Reverse().ToArray())
            If Not plain.StartsWith("ps-") Then Return FaultKind.None
            Dim kind As FaultKind
            If [Enum].TryParse(plain.Substring(3), kind) Then Return kind
        Catch ex As FormatException
        End Try
        Return FaultKind.None
    End Function

    ''' <summary>One or two words for the marker on the drawing.</summary>
    Public Function ShortName(kind As FaultKind) As String
        Select Case kind
            Case FaultKind.Leak : Return "leaking"
            Case FaultKind.Blocked : Return "blocked"
            Case FaultKind.StuckNormal : Return "stuck"
            Case FaultKind.StuckOperated : Return "stuck on"
            Case FaultKind.BurntCoil : Return "coil burnt"
            Case FaultKind.ContactOpen : Return "no contact"
            Case FaultKind.ContactWelded : Return "welded"
            Case FaultKind.Sticking : Return "sticking"
            Case FaultKind.Jammed : Return "jammed"
            Case FaultKind.LowOutput : Return "low output"
            Case Else : Return ""
        End Select
    End Function

    ''' <summary>Speed factor of an actuator with this fault.</summary>
    Public Function SpeedFactor(kind As FaultKind) As Double
        Select Case kind
            Case FaultKind.Jammed : Return 0
            Case FaultKind.Sticking : Return 0.3
            Case FaultKind.Leak : Return 0.4
            Case Else : Return 1
        End Select
    End Function
End Module
