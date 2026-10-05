Module Program
    Private _mainForm As MainForm

    <STAThread>
    Sub Main(args As String())
        Application.EnableVisualStyles()
        Application.SetCompatibleTextRenderingDefault(False)
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException)
        AddHandler Application.ThreadException, Sub(s, e) ReportError(e.Exception)
        AddHandler AppDomain.CurrentDomain.UnhandledException, Sub(s, e) ReportError(TryCast(e.ExceptionObject, Exception))
        _mainForm = New MainForm()
        If args.Length > 0 AndAlso IO.File.Exists(args(0)) Then _mainForm.OpenFile(args(0)) Else _mainForm.OfferRecovery()
        Application.Run(_mainForm)
    End Sub

    ''' <summary>Last line of defence: keep the work safe and tell the user, instead of closing.</summary>
    Private Sub ReportError(ex As Exception)
        Try
            _mainForm?.RecoverFromError()
        Catch
            ' Nothing more can be done here.
        End Try
        MessageBox.Show("Something went wrong inside PneuSim, but your work is safe: it has been saved to the recovery file " &
                        "and the simulation was stopped." & vbCrLf & vbCrLf & "Details: " & If(ex?.Message, "unknown error"),
                        "PneuSim", MessageBoxButtons.OK, MessageBoxIcon.Warning)
    End Sub
End Module
