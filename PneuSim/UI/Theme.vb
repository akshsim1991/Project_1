Imports System.ComponentModel
Imports System.Globalization

''' <summary>Dark-mode colours for dialogs and tab strips (the main window has its own theming).</summary>
Public Module Theme
    Public ReadOnly DarkBack As Color = Color.FromArgb(37, 39, 46)
    Public ReadOnly DarkFore As Color = Color.FromArgb(225, 228, 235)
    Public ReadOnly DarkField As Color = Color.FromArgb(28, 30, 36)
    Public ReadOnly DarkHeader As Color = Color.FromArgb(52, 56, 66)

    ''' <summary>Applies dark colours to a dialog and everything in it (nothing happens in light mode).</summary>
    Public Sub Apply(root As Control)
        If Not AppSettings.DarkMode Then Return
        root.BackColor = DarkBack
        root.ForeColor = DarkFore
        ApplyChildren(root.Controls)
    End Sub

    Private Sub ApplyChildren(controls As Control.ControlCollection)
        For Each c As Control In controls
            Select Case True
                Case TypeOf c Is TextBoxBase, TypeOf c Is ListBox, TypeOf c Is ListView, TypeOf c Is ComboBox, TypeOf c Is NumericUpDown
                    c.BackColor = DarkField : c.ForeColor = DarkFore
                Case TypeOf c Is DataGridView
                    Dim dg = DirectCast(c, DataGridView)
                    dg.BackgroundColor = DarkField
                    dg.GridColor = DarkHeader
                    dg.EnableHeadersVisualStyles = False
                    dg.DefaultCellStyle.BackColor = DarkField
                    dg.DefaultCellStyle.ForeColor = DarkFore
                    dg.DefaultCellStyle.SelectionBackColor = Color.FromArgb(62, 68, 82)
                    dg.ColumnHeadersDefaultCellStyle.BackColor = DarkHeader
                    dg.ColumnHeadersDefaultCellStyle.ForeColor = DarkFore
                    dg.RowHeadersDefaultCellStyle.BackColor = DarkHeader
                Case TypeOf c Is PropertyGrid
                    Dim pg = DirectCast(c, PropertyGrid)
                    pg.ViewBackColor = DarkField : pg.ViewForeColor = DarkFore
                    pg.LineColor = DarkHeader : pg.CategoryForeColor = DarkFore
                    pg.HelpBackColor = DarkBack : pg.HelpForeColor = DarkFore
                    pg.BackColor = DarkBack
                Case TypeOf c Is Button
                    c.BackColor = DarkHeader : c.ForeColor = DarkFore
                    DirectCast(c, Button).FlatStyle = FlatStyle.Flat
                Case TypeOf c Is TabControl
                    OwnerDrawTabs(DirectCast(c, TabControl))
                    c.BackColor = DarkBack : c.ForeColor = DarkFore
                Case TypeOf c Is PictureBox
                    ' Pictures keep their own background.
                Case Else
                    c.BackColor = DarkBack : c.ForeColor = DarkFore
            End Select
            If c.HasChildren Then ApplyChildren(c.Controls)
        Next
    End Sub

    ''' <summary>Draws the tab headers ourselves so they follow the light or dark colours.</summary>
    Public Sub OwnerDrawTabs(tc As TabControl)
        If tc.DrawMode = TabDrawMode.OwnerDrawFixed Then Return
        tc.DrawMode = TabDrawMode.OwnerDrawFixed
        AddHandler tc.DrawItem,
            Sub(s, e)
                Dim dark = AppSettings.DarkMode
                Dim selected = e.Index = tc.SelectedIndex
                Dim back = If(dark, If(selected, DarkHeader, DarkBack), If(selected, SystemColors.Window, SystemColors.Control))
                Dim fore = If(dark, DarkFore, SystemColors.ControlText)
                Using b As New SolidBrush(back)
                    e.Graphics.FillRectangle(b, e.Bounds)
                End Using
                TextRenderer.DrawText(e.Graphics, tc.TabPages(e.Index).Text, tc.Font, e.Bounds, fore,
                                      TextFormatFlags.HorizontalCenter Or TextFormatFlags.VerticalCenter)
            End Sub
    End Sub
End Module

''' <summary>Shows the descriptions of enum values (e.g. "+24 V") instead of their code names in the Properties panel.</summary>
Public Class EnumDescriptionConverter
    Inherits EnumConverter

    Public Sub New(type As Type)
        MyBase.New(type)
    End Sub

    Private Shared Function Describe(value As Object) As String
        Dim field = value.GetType().GetField(value.ToString())
        Dim attr = If(field Is Nothing, Nothing, CType(Attribute.GetCustomAttribute(field, GetType(DescriptionAttribute)), DescriptionAttribute))
        Return If(attr Is Nothing, value.ToString(), attr.Description)
    End Function

    Public Overrides Function ConvertTo(context As ITypeDescriptorContext, culture As CultureInfo, value As Object, destinationType As Type) As Object
        If destinationType Is GetType(String) AndAlso value IsNot Nothing AndAlso value.GetType().IsEnum Then Return Describe(value)
        Return MyBase.ConvertTo(context, culture, value, destinationType)
    End Function

    Public Overrides Function ConvertFrom(context As ITypeDescriptorContext, culture As CultureInfo, value As Object) As Object
        Dim text = TryCast(value, String)
        If text IsNot Nothing Then
            For Each v In [Enum].GetValues(EnumType)
                If String.Equals(Describe(v), text, StringComparison.OrdinalIgnoreCase) Then Return v
            Next
        End If
        Return MyBase.ConvertFrom(context, culture, value)
    End Function
End Class

''' <summary>Hides properties that do not apply to a component's current settings (see <see cref="CircuitElement.ShowProperty"/>).</summary>
Public Class ElementDescriptionProvider
    Inherits TypeDescriptionProvider

    Public Sub New()
        MyBase.New(TypeDescriptor.GetProvider(GetType(Object)))
    End Sub

    Public Overrides Function GetTypeDescriptor(objectType As Type, instance As Object) As ICustomTypeDescriptor
        Return New ElementTypeDescriptor(MyBase.GetTypeDescriptor(objectType, instance), TryCast(instance, CircuitElement))
    End Function

    Private Class ElementTypeDescriptor
        Inherits CustomTypeDescriptor

        Private ReadOnly _element As CircuitElement

        Public Sub New(parent As ICustomTypeDescriptor, element As CircuitElement)
            MyBase.New(parent)
            _element = element
        End Sub

        Public Overrides Function GetProperties() As PropertyDescriptorCollection
            Return Filter(MyBase.GetProperties())
        End Function

        Public Overrides Function GetProperties(attributes As Attribute()) As PropertyDescriptorCollection
            Return Filter(MyBase.GetProperties(attributes))
        End Function

        Private Function Filter(all As PropertyDescriptorCollection) As PropertyDescriptorCollection
            If _element Is Nothing Then Return all
            Return New PropertyDescriptorCollection(all.Cast(Of PropertyDescriptor)().Where(Function(p) _element.ShowProperty(p.Name)).ToArray())
        End Function
    End Class
End Class
