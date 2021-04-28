// ---------------------------------------------------------------------
//
// Copyright (c) 2017-2018 The Regents of the University of Michigan and DFT-FE
// authors.
//
// This file is part of the DFT-FE code.
//
// The DFT-FE code is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE at
// the top level of the DFT-FE udistribution.
//
// ---------------------------------------------------------------------
//
// @author  Phani Motamarri, Sambit Das
//


template <unsigned int FEOrder, unsigned int FEOrderElectro>
void
kohnShamDFTOperatorClass<FEOrder, FEOrderElectro>::
  preComputeShapeFunctionGradientIntegrals(const unsigned int lpspQuadratureId)
{
  //
  // get FE data
  //
  const unsigned int numberMacroCells =
    dftPtr->matrix_free_data.n_macro_cells();
  const unsigned int numberPhysicalCells =
    dftPtr->matrix_free_data.n_physical_cells();
  const Quadrature<3> &quadrature =
    dftPtr->matrix_free_data.get_quadrature(dftPtr->d_densityQuadratureId);
  FEValues<3> fe_values(dftPtr->matrix_free_data.get_dof_handler().get_fe(),
                        quadrature,
                        update_values | update_gradients);
  const unsigned int numberDofsPerElement =
    dftPtr->matrix_free_data.get_dof_handler().get_fe().dofs_per_cell;
  const unsigned int numberQuadraturePoints = quadrature.size();

  QGauss<3>   quadraturePlusOne(FEOrder + 1);
  FEValues<3> fe_values_quadplusone(
    dftPtr->matrix_free_data.get_dof_handler().get_fe(),
    quadraturePlusOne,
    update_gradients | update_JxW_values);
  const unsigned int numberQuadraturePointsPlusOne = quadraturePlusOne.size();

  FEValues<3> fe_values_lpsp(
    dftPtr->matrix_free_data.get_dof_handler().get_fe(),
    dftPtr->matrix_free_data.get_quadrature(lpspQuadratureId),
    update_values);
  const unsigned int numberQuadraturePointsLpsp =
    dftPtr->matrix_free_data.get_quadrature(lpspQuadratureId).size();

  //
  // resize data members
  //
  d_cellShapeFunctionGradientIntegral.resize(numberMacroCells);

  d_shapeFunctionValue.resize(numberQuadraturePoints * numberDofsPerElement,
                              0.0);
  d_shapeFunctionValueLpspQuad.resize(numberQuadraturePointsLpsp *
                                        numberDofsPerElement,
                                      0.0);
  std::vector<std::vector<std::vector<Tensor<1, 3, double>>>>
    tempShapeFuncGradData;

  typename dealii::DoFHandler<3>::active_cell_iterator cellPtr;

  //
  // compute cell-level shapefunctiongradientintegral generator by going over
  // dealii macrocells which allows efficient integration of cell-level matrix
  // integrals using dealii vectorized arrays
  for (int iMacroCell = 0; iMacroCell < numberMacroCells; ++iMacroCell)
    {
      dealii::AlignedVector<VectorizedArray<double>> &shapeFunctionGradients =
        d_cellShapeFunctionGradientIntegral[iMacroCell];
      shapeFunctionGradients.resize(numberDofsPerElement *
                                    numberDofsPerElement);

      unsigned int n_sub_cells =
        dftPtr->matrix_free_data.n_components_filled(iMacroCell);
      tempShapeFuncGradData.resize(n_sub_cells);

      for (unsigned int iCell = 0; iCell < n_sub_cells; ++iCell)
        {
          cellPtr = dftPtr->matrix_free_data.get_cell_iterator(
            iMacroCell, iCell, dftPtr->d_densityDofHandlerIndex);
          fe_values_quadplusone.reinit(cellPtr);

          for (unsigned int iNode = 0; iNode < numberDofsPerElement; ++iNode)
            {
              for (unsigned int jNode = 0; jNode < numberDofsPerElement;
                   ++jNode)
                {
                  double shapeFunctionGradientValue = 0.0;
                  for (unsigned int q_point = 0;
                       q_point < numberQuadraturePointsPlusOne;
                       ++q_point)
                    shapeFunctionGradientValue +=
                      (fe_values_quadplusone.shape_grad(iNode, q_point) *
                       fe_values_quadplusone.shape_grad(jNode, q_point)) *
                      fe_values_quadplusone.JxW(q_point);

                  shapeFunctionGradients[numberDofsPerElement * iNode + jNode]
                                        [iCell] = shapeFunctionGradientValue;
                } // j node loop

            } // i node loop



          if (iMacroCell == 0 && iCell == 0)
            {
              fe_values.reinit(cellPtr);
              fe_values_lpsp.reinit(cellPtr);

              for (unsigned int iNode = 0; iNode < numberDofsPerElement;
                   ++iNode)
                for (unsigned int q_point = 0; q_point < numberQuadraturePoints;
                     ++q_point)
                  d_shapeFunctionValue[numberQuadraturePoints * iNode +
                                       q_point] =
                    fe_values.shape_value(iNode, q_point);

              for (unsigned int iNode = 0; iNode < numberDofsPerElement;
                   ++iNode)
                for (unsigned int q_point = 0;
                     q_point < numberQuadraturePointsLpsp;
                     ++q_point)
                  d_shapeFunctionValueLpspQuad[numberQuadraturePointsLpsp *
                                                 iNode +
                                               q_point] =
                    fe_values_lpsp.shape_value(iNode, q_point);
            }

        } // icell loop

    } // macrocell loop

  //
  // Fill FE datastructures required for density computation from wavefunctions
  //
  d_densityGaussQuadShapeFunctionValues.clear();
  d_densityGaussQuadShapeFunctionGradientValues.clear();

  d_densityGaussQuadShapeFunctionValues.resize(numberQuadraturePoints *
                                                 numberDofsPerElement,
                                               0.0);
  d_densityGaussQuadShapeFunctionGradientValues.resize(
    numberPhysicalCells * numberQuadraturePoints * 3 * numberDofsPerElement,
    0.0);

  cellPtr =
    dftPtr->matrix_free_data.get_dof_handler(dftPtr->d_densityDofHandlerIndex)
      .begin_active();
  typename dealii::DoFHandler<3>::active_cell_iterator endcPtr =
    dftPtr->matrix_free_data.get_dof_handler(dftPtr->d_densityDofHandlerIndex)
      .end();

  unsigned int iElem = 0;
  for (; cellPtr != endcPtr; ++cellPtr)
    if (cellPtr->is_locally_owned())
      {
        fe_values.reinit(cellPtr);

        for (unsigned int q_point = 0; q_point < numberQuadraturePoints;
             ++q_point)

          for (unsigned int iNode = 0; iNode < numberDofsPerElement; ++iNode)
            for (unsigned int q_point = 0; q_point < numberQuadraturePoints;
                 ++q_point)
              {
                const dealii::Tensor<1, 3, double> &shape_grad =
                  fe_values.shape_grad(iNode, q_point);

                d_densityGaussQuadShapeFunctionGradientValues
                  [iElem * numberQuadraturePoints * 3 * numberDofsPerElement +
                   q_point * 3 * numberDofsPerElement + iNode] = shape_grad[0];

                d_densityGaussQuadShapeFunctionGradientValues
                  [iElem * numberQuadraturePoints * 3 * numberDofsPerElement +
                   q_point * 3 * numberDofsPerElement + numberDofsPerElement +
                   iNode] = shape_grad[1];

                d_densityGaussQuadShapeFunctionGradientValues
                  [iElem * numberQuadraturePoints * 3 * numberDofsPerElement +
                   q_point * 3 * numberDofsPerElement +
                   2 * numberDofsPerElement + iNode] = shape_grad[2];
              }

        if (iElem == 0)
          {
            for (unsigned int iNode = 0; iNode < numberDofsPerElement; ++iNode)
              for (unsigned int q_point = 0; q_point < numberQuadraturePoints;
                   ++q_point)
                {
                  const double val = fe_values.shape_value(iNode, q_point);
                  d_densityGaussQuadShapeFunctionValues[q_point *
                                                          numberDofsPerElement +
                                                        iNode] = val;
                }
          }

        iElem++;
      }


  QGaussLobatto<3> quadratureGl(C_rhoNodalPolyOrder<FEOrder, FEOrderElectro>() +
                                1);
  FEValues<3>      fe_valuesGl(dftPtr->matrix_free_data
                            .get_dof_handler(dftPtr->d_densityDofHandlerIndex)
                            .get_fe(),
                          quadratureGl,
                          update_values);
  const unsigned int numberQuadraturePointsGl = quadratureGl.size();

  //
  // resize data members
  //
  d_densityGlQuadShapeFunctionValues.clear();
  d_densityGlQuadShapeFunctionValues.resize(numberQuadraturePointsGl *
                                              numberDofsPerElement,
                                            0.0);

  cellPtr =
    dftPtr->matrix_free_data.get_dof_handler(dftPtr->d_densityDofHandlerIndex)
      .begin_active();
  fe_valuesGl.reinit(cellPtr);

  for (unsigned int iNode = 0; iNode < numberDofsPerElement; ++iNode)
    for (unsigned int q_point = 0; q_point < numberQuadraturePointsGl;
         ++q_point)
      {
        const double val = fe_valuesGl.shape_value(iNode, q_point);
        d_densityGlQuadShapeFunctionValues[q_point * numberDofsPerElement +
                                           iNode] = val;
      }
}
