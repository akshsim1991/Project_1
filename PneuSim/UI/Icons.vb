Imports System.Drawing.Drawing2D

''' <summary>Small toolbar images drawn in code so the project needs no image resources.</summary>
Public Module Icons
    Private Function Draw(paint As Action(Of Graphics)) As Bitmap
        Dim bmp As New Bitmap(16, 16)
        Using g = Graphics.FromImage(bmp)
            g.SmoothingMode = SmoothingMode.AntiAlias
            paint(g)
        End Using
        Return bmp
    End Function

    Public Function NewFile() As Image
        Return Draw(Sub(g)
                        g.FillPolygon(Brushes.White, {New Point(3, 1), New Point(10, 1), New Point(13, 4), New Point(13, 15), New Point(3, 15)})
                        g.DrawPolygon(Pens.DimGray, {New Point(3, 1), New Point(10, 1), New Point(13, 4), New Point(13, 15), New Point(3, 15)})
                    End Sub)
    End Function

    Public Function Open() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.Goldenrod, 1, 4, 14, 10)
                        g.FillRectangle(Brushes.Gold, 1, 6, 14, 8)
                        g.DrawRectangle(Pens.DarkGoldenrod, 1, 4, 14, 10)
                    End Sub)
    End Function

    Public Function Save() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.SteelBlue, 1, 1, 14, 14)
                        g.FillRectangle(Brushes.White, 4, 2, 8, 5)
                        g.FillRectangle(Brushes.LightGray, 4, 10, 8, 5)
                    End Sub)
    End Function

    Public Function Play() As Image
        Return Draw(Sub(g) g.FillPolygon(Brushes.ForestGreen, {New Point(3, 1), New Point(14, 8), New Point(3, 15)}))
    End Function

    Public Function Pause() As Image
        Return Draw(Sub(g)
                        g.FillRectangle(Brushes.DarkOrange, 3, 2, 4, 12)
                        g.FillRectangle(Brushes.DarkOrange, 9, 2, 4, 12)
                    End Sub)
    End Function

    Public Function StopIcon() As Image
        Return Draw(Sub(g) g.FillRectangle(Brushes.Firebrick, 2, 2, 12, 12))
    End Function

    Public Function RotateIcon() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.DimGray, 2)
                            g.DrawArc(p, 2, 2, 12, 12, 30, 280)
                        End Using
                        g.FillPolygon(Brushes.DimGray, {New Point(10, 0), New Point(15, 4), New Point(9, 6)})
                    End Sub)
    End Function

    Public Function DeleteIcon() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.Firebrick, 2.5F)
                            g.DrawLine(p, 3, 3, 13, 13)
                            g.DrawLine(p, 13, 3, 3, 13)
                        End Using
                    End Sub)
    End Function

    Public Function UndoIcon(redo As Boolean) As Image
        Return Draw(Sub(g)
                        If redo Then
                            g.TranslateTransform(16, 0)
                            g.ScaleTransform(-1, 1)
                        End If
                        Using p As New Pen(Color.SteelBlue, 2)
                            g.DrawArc(p, 3, 4, 11, 10, 180, 200)
                        End Using
                        g.FillPolygon(Brushes.SteelBlue, {New Point(0, 6), New Point(7, 6), New Point(3, 12)})
                    End Sub)
    End Function

    Public Function Wand() As Image
        Return Draw(Sub(g)
                        Using p As New Pen(Color.DimGray, 2.5F)
                            g.DrawLine(p, 2, 14, 10, 6)
                        End Using
                        g.FillPolygon(Brushes.Gold, {New Point(12, 0), New Point(13, 3), New Point(16, 4), New Point(13, 5), New Point(12, 8), New Point(11, 5), New Point(8, 4), New Point(11, 3)})
                    End Sub)
    End Function

    Public Function ZoomIn() As Image
        Return Draw(Sub(g) Magnifier(g, True))
    End Function

    Public Function ZoomOut() As Image
        Return Draw(Sub(g) Magnifier(g, False))
    End Function

    Private Sub Magnifier(g As Graphics, plus As Boolean)
        Using p As New Pen(Color.DimGray, 1.6F)
            g.DrawEllipse(p, 1, 1, 10, 10)
            g.DrawLine(p, 4, 6, 8, 6)
            If plus Then g.DrawLine(p, 6, 4, 6, 8)
        End Using
        Using p As New Pen(Color.DimGray, 3)
            g.DrawLine(p, 10, 10, 14, 14)
        End Using
    End Sub

    Public Function CheckIcon() As Image
        Return Draw(Sub(g)
                        g.FillEllipse(Brushes.ForestGreen, 1, 1, 14, 14)
                        Using p As New Pen(Color.White, 2)
                            g.DrawLines(p, {New Point(4, 8), New Point(7, 11), New Point(12, 4)})
                        End Using
                    End Sub)
    End Function

    Public Function RecordIcon() As Image
        Return Draw(Sub(g)
                        g.DrawEllipse(Pens.DimGray, 1, 1, 14, 14)
                        g.FillEllipse(Brushes.Red, 4, 4, 8, 8)
                    End Sub)
    End Function

    Public Function GaugeIcon() As Image
        Return Draw(Sub(g)
                        g.DrawEllipse(Pens.DimGray, 1, 1, 14, 14)
                        Using p As New Pen(Color.Firebrick, 2)
                            g.DrawLine(p, 8, 8, 12, 4)
                        End Using
                    End Sub)
    End Function

    Public Function AppIcon() As Icon
        Using bmp As New Bitmap(32, 32)
            Using g = Graphics.FromImage(bmp)
                g.SmoothingMode = SmoothingMode.AntiAlias
                g.FillEllipse(New SolidBrush(RenderContext.PressureColor), 1, 1, 30, 30)
                g.FillRectangle(Brushes.White, 6, 11, 16, 10)
                g.FillRectangle(Brushes.White, 22, 14, 6, 4)
                g.FillRectangle(New SolidBrush(RenderContext.PressureColor), 13, 12, 3, 8)
            End Using
            Return Icon.FromHandle(bmp.GetHicon())
        End Using
    End Function
End Module
