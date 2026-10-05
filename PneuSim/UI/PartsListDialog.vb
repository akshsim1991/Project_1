Imports System.Globalization

''' <summary>Parts list with editable unit prices and CSV export.</summary>
Public Class PartsListDialog
    Inherits Form

    Private ReadOnly _project As Project
    Private ReadOnly _grid As New DataGridView() With {
        .Dock = DockStyle.Fill, .AllowUserToAddRows = False, .AllowUserToDeleteRows = False, .RowHeadersVisible = False,
        .AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill, .BackgroundColor = Color.White}
    Private ReadOnly _total As New Label() With {.Dock = DockStyle.Bottom, .Height = 28, .TextAlign = ContentAlignment.MiddleRight,
                                                 .Font = New Font("Segoe UI", 10, FontStyle.Bold), .Padding = New Padding(0, 0, 12, 0)}
    Private _lines As List(Of PartLine)

    ''' <summary>True if the user changed a price (the project should be marked as modified).</summary>
    Public Property PricesChanged As Boolean

    Public Sub New(project As Project)
        _project = project
        Text = "Parts list"
        Font = New Font("Segoe UI", 9)
        Size = New Size(820, 520)
        StartPosition = FormStartPosition.CenterParent
        ShowInTaskbar = False
        KeyPreview = True
        AddHandler KeyDown, Sub(s, e)
                                If e.KeyCode = Keys.Escape Then Me.Close()
                            End Sub

        _grid.Columns.Add("qty", "Qty")
        _grid.Columns.Add("desc", "Description")
        _grid.Columns.Add("labels", "Labels")
        _grid.Columns.Add("price", "Unit price (₹)")
        _grid.Columns.Add("total", "Total (₹)")
        _grid.Columns(0).FillWeight = 12 : _grid.Columns(1).FillWeight = 120 : _grid.Columns(2).FillWeight = 60
        _grid.Columns(3).FillWeight = 30 : _grid.Columns(4).FillWeight = 30
        For Each col As DataGridViewColumn In _grid.Columns
            col.ReadOnly = col.Name <> "price"
        Next
        _grid.Columns("price").DefaultCellStyle.BackColor = Color.FromArgb(255, 252, 225)
        AddHandler _grid.CellEndEdit, AddressOf OnPriceEdited

        Dim buttons As New FlowLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = 40, .FlowDirection = FlowDirection.RightToLeft, .Padding = New Padding(6)}
        Dim close As New Button() With {.Text = "Close", .Width = 90, .DialogResult = DialogResult.OK}
        Dim csv As New Button() With {.Text = "Export CSV...", .Width = 110}
        AddHandler csv.Click, AddressOf OnExportCsv
        buttons.Controls.AddRange({close, csv})
        Dim note As New Label() With {.Dock = DockStyle.Top, .Height = 24, .Padding = New Padding(6, 5, 0, 0),
                                      .Text = "Prices are typical estimates; click a price to change it. Your prices are saved with the project."}
        Controls.Add(_grid)
        Controls.Add(note)
        Controls.Add(_total)
        Controls.Add(buttons)
        AcceptButton = close
        Fill()
    End Sub

    Private Sub Fill()
        _lines = PartsList.Build(_project)
        _grid.Rows.Clear()
        For Each l In _lines
            _grid.Rows.Add(l.Quantity, l.Description, l.Labels, l.UnitPrice.ToString("0.00"), l.Total.ToString("#,##0.00"))
        Next
        _total.Text = $"Total: ₹{_lines.Sum(Function(l) l.Total):#,##0.00}"
    End Sub

    Private Sub OnPriceEdited(sender As Object, e As DataGridViewCellEventArgs)
        If e.RowIndex < 0 OrElse e.RowIndex >= _lines.Count Then Return
        Dim raw = Convert.ToString(_grid.Rows(e.RowIndex).Cells("price").Value)
        Dim v As Double
        If Double.TryParse(raw, NumberStyles.Float, CultureInfo.CurrentCulture, v) OrElse
           Double.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, v) Then
            _project.PartCosts(_lines(e.RowIndex).Description) = Math.Max(0, v)
            PricesChanged = True
        End If
        BeginInvoke(New Action(AddressOf Fill))
    End Sub

    Private Sub OnExportCsv(sender As Object, e As EventArgs)
        Using dlg As New SaveFileDialog() With {.Filter = "CSV file (*.csv)|*.csv", .FileName = "parts-list.csv"}
            If dlg.ShowDialog(Me) <> DialogResult.OK Then Return
            IO.File.WriteAllText(dlg.FileName, PartsList.ToCsv(_lines), New Text.UTF8Encoding(True))
        End Using
    End Sub
End Class
