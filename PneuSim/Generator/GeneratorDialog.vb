''' <summary>Lets the user type a motion sequence and generates the complete circuit for it.</summary>
Public Class GeneratorDialog
    Inherits Form

    Private ReadOnly _sequence As New ComboBox() With {.DropDownStyle = ComboBoxStyle.DropDown, .Width = 420, .Font = New Font("Consolas", 11)}
    Private ReadOnly _electro As New RadioButton() With {.Text = "Electro-pneumatic: double-solenoid valves, proximity sensors, relay step chain", .AutoSize = True, .Checked = True}
    Private ReadOnly _cascade As New RadioButton() With {.Text = "Pneumatic: double-pilot valves, roller valves, cascade (group) method", .AutoSize = True}
    Private ReadOnly _continuous As New CheckBox() With {.Text = "Add a switch for continuous cycling", .AutoSize = True}
    Private ReadOnly _explanation As New TextBox() With {.Multiline = True, .ReadOnly = True, .ScrollBars = ScrollBars.Both, .WordWrap = False,
                                                        .Font = New Font("Consolas", 9), .BackColor = Color.White}
    Private ReadOnly _generate As New Button() With {.Text = "Generate circuit", .Width = 140, .Height = 30}

    ''' <summary>The generated circuit after the dialog closed with OK.</summary>
    Public Property Result As GeneratedCircuit

    Public Sub New()
        Text = "Circuit Generator"
        Font = New Font("Segoe UI", 9)
        Size = New Size(820, 640)
        MinimumSize = New Size(640, 480)
        StartPosition = FormStartPosition.CenterParent
        ShowInTaskbar = False

        _sequence.Items.AddRange({"A+ B+ B- A-", "A+ B+ A- B-", "A+ B+ C+ C- B- A-", "A+ B+ B- C+ C- A-",
                                  "A+ (B+ C+) B- C- A-", "A+ A- B+ B-", "A+ B+ B- A- C+ C-"})
        _sequence.Text = "A+ B+ B- A-"

        Dim intro As New Label() With {
            .AutoSize = False, .Dock = DockStyle.Top, .Height = 48, .Padding = New Padding(10, 8, 10, 0),
            .Text = "Type the order in which the cylinders move. A+ means cylinder A extends, A- means it retracts. " &
                    "Put movements that happen together in brackets, e.g. (B+ C+). All cylinders start and end retracted."}

        Dim top As New FlowLayoutPanel() With {.Dock = DockStyle.Top, .Height = 130, .FlowDirection = FlowDirection.TopDown,
                                               .Padding = New Padding(10, 0, 10, 0), .WrapContents = False}
        Dim seqRow As New FlowLayoutPanel() With {.AutoSize = True, .WrapContents = False}
        seqRow.Controls.Add(New Label() With {.Text = "Sequence:", .AutoSize = True, .Padding = New Padding(0, 7, 0, 0)})
        seqRow.Controls.Add(_sequence)
        top.Controls.AddRange({seqRow, _electro, _cascade, _continuous})

        Dim bottom As New FlowLayoutPanel() With {.Dock = DockStyle.Bottom, .Height = 46, .FlowDirection = FlowDirection.RightToLeft,
                                                  .Padding = New Padding(8)}
        Dim cancel As New Button() With {.Text = "Cancel", .Width = 90, .Height = 30, .DialogResult = DialogResult.Cancel}
        bottom.Controls.AddRange({cancel, _generate})
        AcceptButton = _generate
        CancelButton = cancel

        Dim explainPanel As New Panel() With {.Dock = DockStyle.Fill, .Padding = New Padding(10, 4, 10, 4)}
        _explanation.Dock = DockStyle.Fill
        explainPanel.Controls.Add(_explanation)
        explainPanel.Controls.Add(New Label() With {.Text = "How the circuit will work:", .Dock = DockStyle.Top, .Height = 20,
                                                   .Font = New Font("Segoe UI", 9, FontStyle.Bold)})

        Controls.Add(explainPanel)
        Controls.Add(bottom)
        Controls.Add(top)
        Controls.Add(intro)

        AddHandler _sequence.TextChanged, Sub() Preview()
        AddHandler _electro.CheckedChanged, Sub() Preview()
        AddHandler _continuous.CheckedChanged, Sub() Preview()
        AddHandler _generate.Click, AddressOf OnGenerate
        Preview()
    End Sub

    Private ReadOnly Property Method As GeneratorMethod
        Get
            Return If(_cascade.Checked, GeneratorMethod.PneumaticCascade, GeneratorMethod.ElectroRelayChain)
        End Get
    End Property

    Private Function TryGenerate() As GeneratedCircuit
        Dim seq = MotionSequence.Parse(_sequence.Text)
        Return SequenceGenerator.Generate(seq, Method, _continuous.Checked)
    End Function

    Private Sub Preview()
        Try
            Dim r = TryGenerate()
            _explanation.Text = r.Explanation.Replace(vbLf, vbCrLf).Replace(vbCr & vbCrLf, vbCrLf)
            _explanation.ForeColor = Color.Black
            _generate.Enabled = True
        Catch ex As FormatException
            _explanation.Text = ex.Message
            _explanation.ForeColor = Color.DarkRed
            _generate.Enabled = False
        End Try
    End Sub

    Private Sub OnGenerate(sender As Object, e As EventArgs)
        Try
            Result = TryGenerate()
            DialogResult = DialogResult.OK
            Close()
        Catch ex As FormatException
            MessageBox.Show(ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning)
        End Try
    End Sub
End Class
