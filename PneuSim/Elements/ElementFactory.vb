''' <summary>Creates elements from their file type name.</summary>
Public Module ElementFactory
    Public Function Create(typeName As String) As CircuitElement
        Select Case typeName
            Case "AirSupply" : Return New AirSupply()
            Case "PressureGauge" : Return New PressureGauge()
            Case "SingleActingCylinder" : Return New SingleActingCylinder()
            Case "DoubleActingCylinder" : Return New DoubleActingCylinder()
            Case "SemiRotaryActuator" : Return New SemiRotaryActuator()
            Case "AirMotor" : Return New AirMotor()
            Case "Valve22" : Return New Valve22()
            Case "Valve32" : Return New Valve32()
            Case "Valve42" : Return New Valve42()
            Case "Valve52" : Return New Valve52()
            Case "Valve53" : Return New Valve53()
            Case "ShuttleValve" : Return New ShuttleValve()
            Case "TwoPressureValve" : Return New TwoPressureValve()
            Case "FlowControlValve" : Return New FlowControlValve()
            Case "CheckValve" : Return New CheckValve()
            Case "QuickExhaustValve" : Return New QuickExhaustValve()
            Case "PressureRegulator" : Return New PressureRegulator()
            Case "Silencer" : Return New Silencer()
            Case "Junction" : Return New Junction()
            Case "TextNote" : Return New TextNote()
            Case "PageConnector" : Return New PageConnector()
            Case "HydraulicPump" : Return New HydraulicPump()
            Case "HydraulicTank" : Return New HydraulicTank()
            Case "ReliefValve" : Return New ReliefValve()
            Case "HydraulicCylinder" : Return New HydraulicCylinder()
            Case "HydraulicMotor" : Return New HydraulicMotor()
            Case "Accumulator" : Return New Accumulator()
            Case "HydraulicValve43" : Return New HydraulicValve43()
            Case "HydraulicValve42" : Return New HydraulicValve42()
            Case "CompensatedFlowControl" : Return New CompensatedFlowControl()
            Case "CounterbalanceValve" : Return New CounterbalanceValve()
            Case "PowerTerminal" : Return New PowerTerminal()
            Case "ElectricContact" : Return New ElectricContact()
            Case "ElectricCoil" : Return New ElectricCoil()
            Case Else : Return Nothing
        End Select
    End Function
End Module
